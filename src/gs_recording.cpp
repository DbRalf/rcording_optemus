#include <gst/gst.h>
#include <gst/rtsp/gstrtspurl.h>
#include <iostream>
#include <memory>


static gchar * on_format_location(GstElement *, guint fragment, gpointer user_data)
{
    const gchar *feed_name = (const gchar *)user_data;
    gchar *name = g_strdup_printf("%s_%05d.mp4", feed_name, fragment);
    g_print("[sink] writing segment -> %s\n", name);
    return name;
}


static GstPadProbeReturn fps_probe(GstPad *, GstPadProbeInfo *info, gpointer user_data)
{
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (GST_BUFFER_PTS_IS_VALID(buf)) {
        GstClockTime pts = GST_BUFFER_PTS(buf);
        g_print("[fps] %s  pts=%" GST_TIME_FORMAT "\n",
                (const gchar *)user_data,
                GST_TIME_ARGS(pts));
    }
    return GST_PAD_PROBE_OK;
}


static void pad_added_rtsp(GstElement *src, GstPad *new_pad, GstElement *depay)
{
    GstPad *sink_pad = gst_element_get_static_pad(depay, "sink");

    if (gst_pad_is_linked(sink_pad)) {
        gst_object_unref(sink_pad);
        return;
    }

    GstCaps *new_pad_caps = gst_pad_get_current_caps(new_pad);
    GstStructure *new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    const gchar *new_pad_type = gst_structure_get_name(new_pad_struct);

    if (!g_str_has_prefix(new_pad_type, "application/x-rtp")) {
        g_print("Ignoring non-RTP pad type: %s\n", new_pad_type);
        gst_caps_unref(new_pad_caps);
        gst_object_unref(sink_pad);
        return;
    }

    if (gst_pad_link(new_pad, sink_pad) != GST_PAD_LINK_OK)
        g_printerr("Failed to link rtspsrc pad to depay\n");
    else
        g_print("[rtsp] pad linked -> depay\n");

    gst_caps_unref(new_pad_caps);
    gst_object_unref(sink_pad);
}


GstElement * build_recording_pipeline(const std::string &feed_name, const std::string &rtsp_url, const std::string &output_dir)
{
    // lambda so it wont look ugly af
    auto create_element = [&](const char * type, const char * element_name){
        std::string name = feed_name + element_name;
        return gst_element_factory_make(type, name.c_str());
    };
    // pipeline
    GstElement * pipeline = gst_pipeline_new((feed_name + "-pipeline").c_str());
    
    // elements
    GstElement * src = create_element("rtspsrc", "src");
    GstElement * depay = create_element("rtph265depay", "depay");
    GstElement * parse_1 = create_element("h265parse", "parse1");
    GstElement * decode = create_element("avdec_h265", "decode");
    GstElement * rate = create_element("videorate", "rate");
    GstElement * encode = create_element("x265enc", "encode");
    GstElement * parse_2 = create_element("h265parse", "parse2");
    GstElement * queue = create_element("queue", "queue");
    GstElement * sink = create_element("splitmuxsink", "sink");

    if(!pipeline || !src || !depay || !parse_1 || !decode || !rate || !encode || !parse_2 || !queue || !sink){
        g_printerr("unable to create all element! crash.. ");
        return NULL;
    }

    // set properties
    g_object_set(G_OBJECT(src),
                "location", rtsp_url.c_str(),
                "protocols", GST_RTSP_LOWER_TRANS_UDP,
                "drop-on-latency", TRUE,
                "udp-reconnect", TRUE,
                "timeout", 0,
                "is-live", TRUE,
                NULL);

    g_object_set(G_OBJECT(parse_1),
                "config-interval", 1,
                NULL);
    

    g_object_set(G_OBJECT(rate),
                "drop-only", TRUE,
                "max-rate", 1,
                NULL);

    g_object_set(G_OBJECT(encode),
                "bframes", 0,  // disable B-frames: keeps output in timestamp order
                NULL);

    // hard set caps
    GstCaps * caps = gst_caps_new_simple(
                                        "video/x-raw",
                                        "framerate", GST_TYPE_FRACTION, 1, 1,
                                        NULL);
    
    g_object_set(G_OBJECT(queue),
                "leaky", 2,  // leak downstream (drop old frames) when full
                NULL);

    g_object_set(G_OBJECT(sink),
                "max-size-time", 60 * GST_SECOND,  // ~60s actual: compensates for camera clock drift
                "async-finalize", TRUE,
                "muxer-factory", "mp4mux",
                NULL);


    gst_bin_add_many(GST_BIN(pipeline), src, depay, parse_1, decode,
                             rate, encode, parse_2, queue, sink,
                             NULL);

    // linking static pads
    gst_element_link_many(depay, parse_1, decode, rate, NULL);
    gst_element_link_filtered(rate, encode, caps);
    gst_caps_unref(caps);
    gst_element_link_many(encode, parse_2, queue, sink, NULL);

    GstPad *rate_src = gst_element_get_static_pad(rate, "src");
    gst_pad_add_probe(rate_src, GST_PAD_PROBE_TYPE_BUFFER, fps_probe, g_strdup(feed_name.c_str()), g_free);
    gst_object_unref(rate_src);

    g_signal_connect(src, "pad-added", G_CALLBACK(pad_added_rtsp), depay);
    std::string path_prefix = output_dir + "/" + feed_name;
    g_signal_connect(sink, "format-location", G_CALLBACK(on_format_location), g_strdup(path_prefix.c_str()));

    g_print("[pipeline] '%s' created\n", (feed_name + "-pipeline").c_str());
    return pipeline;
}   


struct PipelineCtx {
    GstElement *pipeline;
    GMainLoop  *loop;
    int        *active_count;
};

static gboolean restart_pipeline(gpointer data)
{
    PipelineCtx *ctx = (PipelineCtx *)data;
    g_print("[retry] restarting %s\n", GST_OBJECT_NAME(ctx->pipeline));
    gst_element_set_state(ctx->pipeline, GST_STATE_PLAYING);
    return G_SOURCE_REMOVE;
}

static gboolean bus_callback(GstBus *bus, GstMessage *msg, gpointer data)
{
    PipelineCtx *ctx = (PipelineCtx *)data;
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("[bus] EOS on %s\n", GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)));
            if (--(*ctx->active_count) == 0) {
                g_print("[bus] all pipelines done\n");
                g_main_loop_quit(ctx->loop);
            }
            break;
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(msg, &err, &debug);
            g_printerr("[bus] error on %s: %s\n",
                       GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)), err->message);
            g_error_free(err);
            g_free(debug);
            gst_element_set_state(ctx->pipeline, GST_STATE_NULL);
            g_print("[bus] stream down, retrying in 5s...\n");
            g_timeout_add_seconds(5, restart_pipeline, ctx);
            break;
        }
        default:
            break;
    }
    return TRUE;
}


int main(int argc, char * argv[])
{
    gst_init(&argc, &argv);

    // build timestamped output directory tree
    GDateTime *dt = g_date_time_new_now_local();
    gchar *ts = g_date_time_format(dt, "%Y%m%d_%H%M%S");
    g_date_time_unref(dt);

    std::string dirs[4] = {
        std::string(ts) + "/front/rgb",
        std::string(ts) + "/rear/rgb",
        std::string(ts) + "/front/thermal",
        std::string(ts) + "/rear/thermal",
    };
    g_free(ts);

    for (const auto &d : dirs)
        g_mkdir_with_parents(d.c_str(), 0755);

    GstElement *pipelines[4] = {
        build_recording_pipeline("rgb_front",     "rtsp://127.0.0.1:8554/stream0/ch1", dirs[0]),
        build_recording_pipeline("rgb_back",      "rtsp://127.0.0.1:8554/stream0/ch2", dirs[1]),
        build_recording_pipeline("thermal_front", "rtsp://127.0.0.1:8554/stream0/ch3", dirs[2]),
        build_recording_pipeline("thermal_back",  "rtsp://127.0.0.1:8554/stream0/ch4", dirs[3]),
    };

    for (int i = 0; i < 4; i++) {
        if (!pipelines[i]) {
            g_printerr("Failed to build pipeline %d\n", i);
            return -1;
        }
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    int active_count = 0;
    PipelineCtx ctxs[4];

    g_print("[main] starting pipelines...\n");
    for (int i = 0; i < 4; i++) {
        if (!pipelines[i]) continue;
        ctxs[i] = { pipelines[i], loop, &active_count };
        GstBus *bus = gst_element_get_bus(pipelines[i]);
        gst_bus_add_watch(bus, bus_callback, &ctxs[i]);
        gst_object_unref(bus);
        gst_element_set_state(pipelines[i], GST_STATE_PLAYING);
        active_count++;
    }

    if (active_count == 0) {
        g_printerr("[main] no pipelines built, exiting\n");
        g_main_loop_unref(loop);
        return -1;
    }
    g_print("[main] %d pipeline(s) waiting for streams...\n", active_count);

    g_main_loop_run(loop);

    for (int i = 0; i < 4; i++) {
        if (!pipelines[i]) continue;
        gst_element_set_state(pipelines[i], GST_STATE_NULL);
        gst_object_unref(pipelines[i]);
    }
    g_main_loop_unref(loop);

    return 0;
}
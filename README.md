# Recording System Optimus

A GStreamer-based recorder that captures 4 RTSP streams (2 RGB + 2 thermal) at 1fps into segmented mp4 files.

## Prerequisites

- GStreamer 1.0 with plugins: `gstreamer1.0-plugins-good`, `gstreamer1.0-plugins-bad`
- An RTSP server on `rtsp://127.0.0.1:8554` (e.g. [MediaMTX](https://github.com/bluenviron/mediamtx))
- Source streams must be H.265 encoded
- `ffmpeg` (for concatenation)

## Build

```bash
mkdir -p build && cd build
cmake ..
make
```

The executable will be at `build/recorder`.

## Run

```bash
./build/recorder
```

The program starts immediately and waits for the RTSP streams to come online. When a stream connects it starts recording automatically. If a stream drops it retries every 5 seconds.

## Output Structure

A timestamped directory is created on startup:

```
20260311_143022/
  front/
    rgb/      rgb_front_00000.mp4  rgb_front_00001.mp4 ...
    thermal/  thermal_front_00000.mp4 ...
  rear/
    rgb/      rgb_back_00000.mp4 ...
    thermal/  thermal_back_00000.mp4 ...
```

Each segment is ~1 minute. If a stream crashes only the current in-progress segment is lost.

## Concatenate Segments into One File

To merge all segments of a feed into a single file:

```bash
cd 20260311_143022/front/thermal
printf "file '%s'\n" *.mp4 > concat_list.txt
ffmpeg -f concat -safe 0 -i concat_list.txt -c copy thermal_front_full.mp4
```

> The zero-padded filenames (`_00000`, `_00001`...) ensure the glob sorts in the correct order.

## Test Stream (GStreamer)

Publish a local H.265 file as an RTSP stream for testing:

```bash
gst-launch-1.0 -e \
  filesrc location=/path/to/your/video.mp4 ! qtdemux name=demux \
  demux.video_0 \
  ! h265parse config-interval=-1 \
  ! queue \
  ! rtspclientsink location=rtsp://127.0.0.1:8554/stream0/ch1 protocols=udp
```

> Replace `/path/to/your/video.mp4` with the actual path to a file on your computer (e.g. `/home/user/videos/sample.mp4`).
> Change the channel (`ch1`–`ch4`) to test each pipeline independently.

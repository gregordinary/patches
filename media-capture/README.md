# Media capture patches

Patches that let a capture source feed the hardware pipeline directly. A capture
driver writes each image into a buffer a hardware scaler or encoder can read. These
patches hand that buffer on, rather than a copy of it.

[`series/media-capture.toml`](../series/media-capture.toml) binds them. Its `ffmpeg`
scope applies on top of whichever FFmpeg provider series the build carries.

Base: FFmpeg `n9.1` with the `rk3588-accel` FFmpeg scope applied, and FFmpeg `n9.1`
with the `rk3576-media` FFmpeg scope applied. Neither provider series touches
`libavdevice` or `avformat_free_context()`, so the patches read the same on an
unpatched `n9.1`.

## Series

| patch | what it does |
| --- | --- |
| `ffmpeg/0001` | Free the packets the framework buffered for a demuxer before the demuxer's private data, since a packet's free callback can read that data. |
| `ffmpeg/0002` | Stop the V4L2 input device re-queuing a buffer that is released after the device closed. |
| `ffmpeg/0003` | Add a `drm_prime` option to the V4L2 input device. It outputs DRM PRIME frames that reference the capture buffers, instead of copying each frame into system memory. |

`0001` and `0002` fix the stock copying path as well, and `0003` depends on both.

## Releasing a capture buffer after close

The V4L2 input device hands out packets whose memory is a mapped capture buffer. The
packet's free callback returns the buffer to the driver, and it reads the device's
private data to do that. `avformat_close_input()` runs the device's `read_close`, and
then `avformat_free_context()`, which used to free the private data before the packets
the framework still held: the probe packets `avformat_find_stream_info()` keeps, and
the parse and raw queues.

So any program that closed an input before reading everything probed ran the callback
on freed memory. `ffmpeg` does this whenever its output fails to open. AddressSanitizer
reports a heap-use-after-free in the callback, reached from `ff_packet_list_free()`,
on the unpatched copying path as much as on `drm_prime`. `0001` frees the buffered
packets first. `0002` makes a buffer released after `read_close` skip the re-queue,
which otherwise logs `ioctl(VIDIOC_QBUF): Bad file descriptor` once per packet.

## `drm_prime` on the V4L2 input device

```sh
ffmpeg -f v4l2 -drm_prime 1 -input_format nv12 -i /dev/video0 \
       -vf hwdownload,format=nv12 -f framemd5 -
```

Each capture buffer is exported once, as a DMA-BUF, when capture starts. Every
dequeued buffer is handed out as a `drm_prime` frame, in a `wrapped_avframe` packet,
the way `kmsgrab` hands out scanout buffers. The frames context takes the capture
format as its software format, so `hwdownload` brings a frame to system memory, and a
hardware filter or encoder imports the buffer where it lies. A buffer returns to the
driver when the last reference to its frame is released.

The plane layout is the driver's, not the frame's. The pitch comes from
`bytesperline` and the completeness check from `sizeimage`, because a receiver can pad
each line. The Synopsys HDMI RX driver aligns every line to 64 bytes, so a
1366-pixel NV12 line is 1408 bytes long. The copying path assumes packed lines, and
rejects every frame of such a mode as the wrong size.

### When the consumer holds frames

The copying path copies once too few buffers remain queued, and a frame that
references its buffer cannot. In this mode the frame is dropped instead, and its buffer
is re-queued at once, so the driver keeps capturing the newest image. The first drop
is a warning that names how many buffers the consumer holds, and the count is logged at
close. The margin needs a third buffer, so the mode refuses a driver that grants fewer
than three.

A consumer that has to hold more frames than the driver granted buffers, before it
releases any, is never handed another frame. `reverse`, or `loop` with a large `size`,
placed ahead of `hwdownload`, stalls that way. Download to system memory before such a
filter. RGA, the MPP encoder and `hwdownload` release a frame within a few frames.

### Formats and options

Formats: the single-plane ones that the device's format table maps and that have a DRM
equivalent. Those are `nv12`, `nv16`, `nv24`, `yuyv422`, `uyvy422`, `bgr24` and `rgb24`.
The mode needs FFmpeg built with libdrm, and it cannot be combined with `-use_libv4l2`.

`-drm_device` names the DRM device the frames context is created on, by default
`/dev/dri/renderD128`. Nothing is allocated from it, so any DRM node serves.

## Validation

On an x86_64 host, against the kernel's `vivid` driver with one single-planar and one
multiplanar instance:

- Every format above hands out byte-identical frames through `drm_prime` and
  `hwdownload` as through the copying path, compared by `framemd5` over a static
  picture.
- With vivid's frame counter on, 120 of 120 frames are distinct, so no buffer is
  handed out twice.
- The open file descriptors hold at 37 over a 1200-frame run: the standard three, the
  device, the DRM node and 32 exported buffers.
- A consumer holding 20 frames runs clean. One holding 40 warns
  `the consumer holds 27 of 32 capture buffers` and stalls as described above.
- Under AddressSanitizer, a normal run and an early exit are clean in both modes.
  Without `0001`, the early exit is the heap-use-after-free described above.

Hardware import by RGA and the MPP encoder on an RK3588 is not validated yet.

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <string.h>

#define VERBOSE 0

#define VTYTPE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
struct bufSt{
    void *start[VIDEO_MAX_PLANES];
    size_t length[VIDEO_MAX_PLANES];
} *buffers;

int bufferIndex = 0;
unsigned int width = 3840;
unsigned int height = 2160;
unsigned int format = V4L2_PIX_FMT_NV12;
unsigned int num_planes = VIDEO_MAX_PLANES;
unsigned int num_buffers = 4;

const char* fmt2str( unsigned fmt )
{
    static char retstr[5] = {0};
    memset( retstr, 0, 5 );
    retstr[0] = fmt & 0xFF;
    retstr[1] = (fmt >> 8) & 0xFF;
    retstr[2] = (fmt >> 16) & 0xFF;
    retstr[3] = (fmt >> 24) & 0xFF;
    return retstr;
}

int get_format(int fd, struct v4l2_format *fmt) {
    return ioctl(fd, VIDIOC_G_FMT, fmt);
}

int request_buffers(int fd) {
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.type = VTYTPE;
    req.memory = V4L2_MEMORY_MMAP;
    req.flags = 0;
    req.count = num_buffers;

    int ret = ioctl(fd, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        perror("VIDIOC_REQBUFS");
        printf("Errno: %d\n", errno);
        return ret;
    }
    num_buffers = req.count;
    #if VERBOSE
    printf("VIDIOC_REQBUFS ok %d alloued\n", num_buffers);
    #endif
    buffers = static_cast<bufSt*>(calloc(num_buffers, sizeof(bufSt)));
    for (size_t i = 0; i < num_buffers; ++i) {
        v4l2_buffer buf = v4l2_buffer();
        v4l2_plane mplanes[VIDEO_MAX_PLANES];
        buf.type = VTYTPE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = mplanes;
        buf.length = VIDEO_MAX_PLANES;
        ret = ioctl(fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0) {
            perror("VIDIOC_QUERYBUF");
            return ret;
        }
        #if VERBOSE
        printf("VIDIOC_QUERYBUF ok\n");
        #endif
        num_planes = buf.length;
        for (size_t j = 0; j < num_planes; ++j) {
            buffers[i].length[j] = buf.m.planes[j].length;
            buffers[i].start[j] = mmap(NULL /* start anywhere */,
                     buffers[i].length[j],
                     PROT_READ /* required */,
                     MAP_SHARED /* recommended */,
                     fd, buf.m.planes[j].m.mem_offset);
            if (buffers[i].start == MAP_FAILED) {
                perror("mmap");
                printf("Errno: %d\n", errno);
                return -EXIT_FAILURE;
            }
            #if VERBOSE
            printf("mmap ok\n");
            #endif
        }
    }
    return 0;
}

int queue_buffer(int fd, int index) {
    v4l2_buffer buf = v4l2_buffer();
    v4l2_plane mplanes[VIDEO_MAX_PLANES];
    buf.type = VTYTPE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.m.planes = mplanes;
    buf.length = VIDEO_MAX_PLANES;

    int ret = -1;
    ret = ioctl(fd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
        perror("VIDIOC_QBUF");
        return ret;
    }
    #if VERBOSE
    printf("VIDIOC_QBUF ok\n");
    #endif
    return ret;
}

int dequeue_buffer(int fd) {
    v4l2_buffer buf = v4l2_buffer();
    v4l2_plane mplanes[VIDEO_MAX_PLANES];
    buf.type = VTYTPE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    buf.m.planes = mplanes;
    buf.length = VIDEO_MAX_PLANES;

    int ret = -1;
    ret = ioctl(fd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        perror("VIDIOC_DQBUF");
        return ret;
    }
    #if VERBOSE
    printf("VIDIOC_DQBUF ok\n");
    #endif
    bufferIndex = buf.index;
    return ret;
}

int start_capture(int fd) {
    uint type = VTYTPE;
    return ioctl(fd, VIDIOC_STREAMON, &type);
}

int stop_capture(int fd) {
    uint type = VTYTPE;
    return ioctl(fd, VIDIOC_STREAMOFF, &type);
}

int process_frame(size_t index) {
    //Image is at `buffers[index].start[j]`
    printf("Got image from buffer %d\n", index);
    return EXIT_SUCCESS;
}

int cleanup(int fd) {
    for (size_t i = 0; i < num_buffers; ++i) {
        for(size_t j = 0; j < num_planes; ++j){
            if (munmap(buffers[i].start[j], buffers[i].length[j]) == -1) { // Unmap each plane separately
                perror("munmap");
                return -EXIT_FAILURE;
            }
        }
    }
    #if VERBOSE
    printf("munmap ok\n");
    #endif

    if (close(fd) < 0) { // Close the device file descriptor
        perror("close");
        return -EXIT_FAILURE;
    }
    #if VERBOSE
    printf("close ok\n");
    #endif

    return EXIT_SUCCESS;
}

int main() {
    int fd = open("/dev/video0", O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        std::cerr << "Failed to open video device" << std::endl;
        return 1;
    }
    #if VERBOSE
    printf("open ok\n");
    #endif

    v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = VTYTPE;
    int ret;
    ret = get_format(fd, &fmt);
    if (ret < 0) {
        perror("get_format");
        return EXIT_FAILURE;
    }
    #if VERBOSE
    printf("get_format ok\n");
    #endif
    width = fmt.fmt.pix_mp.width;
    height = fmt.fmt.pix_mp.height;
    format = fmt.fmt.pix_mp.pixelformat;
    #if VERBOSE
    printf("width: %d\n", width);
    printf("height: %d\n", height);
    printf("pixelformat: %s\n", fmt2str(fmt.fmt.pix_mp.pixelformat));
    printf("field: %d\n", fmt.fmt.pix_mp.field);
    printf("colorspace: %d\n", fmt.fmt.pix_mp.colorspace);
    printf("num_planes: %d\n", fmt.fmt.pix_mp.num_planes);
    #endif

    ret = request_buffers(fd);
    if (ret < 0) perror("request_buffers");
    ret = -1;
    for(int i = 0; i < num_buffers; i++) {
        ret = queue_buffer(fd, i);
        if (ret < 0) perror("queue_buffer");
    }
    ret = start_capture(fd);
    if (ret < 0) perror("start_capture");

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = {0};
    tv.tv_sec = 2;
    int r;
    r = select(fd+1, &fds, NULL, NULL, &tv);
    if(-1 == r){
        perror("Waiting for Frame");
        exit(1);
    }

    ret = dequeue_buffer(fd);
    if (ret < 0) {
        perror("dequeue_buffer");
        return cleanup(fd);
    }
    ret = process_frame(bufferIndex);
    if (ret < 0) {
        perror("process_frame");
        return cleanup(fd);
    }
    queue_buffer(fd, bufferIndex);
    if (ret < 0) {
        perror("queue_buffer");
        return cleanup(fd);
    }

    stop_capture(fd);
    if (ret < 0) perror("stop_capture");
    return cleanup(fd);
}

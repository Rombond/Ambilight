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

#define VTYTPE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
const unsigned int PLANE_COUNT = 1;
const unsigned int BUFFER_COUNT = 2;
void* buffersPtr[BUFFER_COUNT][PLANE_COUNT];
struct v4l2_plane planes[BUFFER_COUNT][PLANE_COUNT];
struct v4l2_buffer buffers[BUFFER_COUNT];

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

int request_buffers(int fd, uint32_t width, uint32_t height) {
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.type = VTYTPE;
    req.memory = V4L2_MEMORY_MMAP;
    req.flags = 0;
    req.count = BUFFER_COUNT;

    int ret = ioctl(fd, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        perror("VIDIOC_REQBUFS");
        printf("Errno: %d\n", errno);
        return ret;
    }
    printf("VIDIOC_REQBUFS ok %d alloued\n", req.count);
    for (size_t i = 0; i < req.count; ++i) {

        memset(&buffers[i], 0, sizeof(buffers[i]));
        memset(&planes[i], 0, sizeof(planes[i]));
        buffers[i].type = VTYTPE;
        buffers[i].memory = V4L2_MEMORY_MMAP;
        buffers[i].index = i;
        buffers[i].m.planes = planes[i];
        buffers[i].length = PLANE_COUNT;
        ret = ioctl(fd, VIDIOC_QUERYBUF, &buffers[i]);
        if (ret < 0) {
            perror("VIDIOC_QUERYBUF");
            return ret;
        }
        printf("VIDIOC_QUERYBUF ok\n");
        for (size_t j = 0; j < PLANE_COUNT; ++j) {
            buffersPtr[i][j] = mmap(NULL, buffers[i].m.planes[j].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffers[i].m.planes[j].m.mem_offset);
            if (buffersPtr[i][j] == MAP_FAILED) {
                perror("mmap");
                printf("Errno: %d\n", errno);
                return -EXIT_FAILURE;
            }
            printf("mmap ok\n");
        }
    }
    return 0;
}

int queue_buffer(int fd, int index) {
    int ret = -1;
    ret = ioctl(fd, VIDIOC_QBUF, &buffers[index]);
    if (ret < 0) {
        perror("VIDIOC_QBUF");
        return ret;
    }
    printf("VIDIOC_QBUF ok\n");
    return ret;
}

int dequeue_buffer(int fd) {
    int ret = -1;
    ret = ioctl(fd, VIDIOC_DQBUF, &buffers[0]);
    if (ret < 0) {
        perror("VIDIOC_DQBUF");
        return ret;
    }
    printf("VIDIOC_DQBUF ok\n");
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
    printf("index: %d\n", index);
    printf("NbPlanes: %d\n", buffers[index].length);
    printf("plane bytesused: %d\n", buffers[index].m.planes[0].bytesused);
    printf("plane length: %d\n", buffers[index].m.planes[0].length);
    printf("plane data_offset: %u\n", buffers[index].m.planes[0].data_offset);
    printf("plane mem_offset: %u\n", buffers[index].m.planes[0].m.mem_offset);
    int file = open("output.yuv", O_WRONLY | O_CREAT, 0666);
	for(size_t j = 0; j < PLANE_COUNT; ++j){
        ssize_t written = write(file, buffersPtr[index][j], buffers[index].m.planes[j].bytesused); // Write each plane to the file
        if (written == -1) {
            perror("write");
            close(file);
            return -EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

int cleanup(int fd) {
    for (size_t i = 0; i < BUFFER_COUNT; ++i) {
        for(size_t j = 0; j < PLANE_COUNT; ++j){
            if (munmap(buffersPtr[i][j], buffers[i].m.planes[j].length) == -1) { // Unmap each plane separately
                perror("munmap");
                return -EXIT_FAILURE;
            }
        }
    }

    if (close(fd) < 0) { // Close the device file descriptor
        perror("close");
        return -EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int main() {
    int fd = open("/dev/video0", O_RDWR, 0);
    if (fd < 0) {
        std::cerr << "Failed to open video device" << std::endl;
        return 1;
    }

    v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = VTYTPE;
    int ret;
    ret = get_format(fd, &fmt);
    if (ret < 0) {
        perror("get_format");
        return EXIT_FAILURE;
    }

    if (1 == 1) {
        printf("width: %d\n", fmt.fmt.pix_mp.width);
        printf("height: %d\n", fmt.fmt.pix_mp.height);
        printf("pixelformat: %s\n", fmt2str(fmt.fmt.pix_mp.pixelformat));
        printf("field: %d\n", fmt.fmt.pix_mp.field);
        printf("colorspace: %d\n", fmt.fmt.pix_mp.colorspace);
        printf("num_planes: %d\n", fmt.fmt.pix_mp.num_planes);
    }

    ret = request_buffers(fd, fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height);
    if (ret < 0) perror("request_buffers");
    ret = -1;
    for(int i = 0; i < BUFFER_COUNT; i++)
        ret = queue_buffer(fd, i);
    if (ret < 0) perror("queue_buffer");
    ret = start_capture(fd);
    if (ret < 0) perror("start_capture");

    int index = dequeue_buffer(fd);
    if (index < 0) {
        perror("queue_buffer");
        return EXIT_FAILURE;
    } 

    process_frame(index);
    if (ret < 0) perror("process_frame");
    queue_buffer(fd, index);
    if (ret < 0) perror("queue_buffer");

    stop_capture(fd);
    if (ret < 0) perror("stop_capture");
    return cleanup(fd);
}
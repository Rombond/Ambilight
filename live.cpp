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
#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>
#include <csignal>

#define VERBOSE 0

#define VTYTPE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
const unsigned int BUFFER_COUNT = 4;
const unsigned int PLANE_COUNT = 1;
struct bufSt{
    void *start[PLANE_COUNT];
    size_t length[PLANE_COUNT];
} *buffers;

int bufferIndex = 0;
unsigned int width = 3840;
unsigned int height = 2160;
unsigned int format = V4L2_PIX_FMT_NV12;
bool loop = true;
cv::Mat bgr_output;

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
    req.count = BUFFER_COUNT;

    int ret = ioctl(fd, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        perror("VIDIOC_REQBUFS");
        printf("Errno: %d\n", errno);
        return ret;
    }
    #if VERBOSE
    printf("VIDIOC_REQBUFS ok %d alloued\n", req.count);
    #endif
    buffers = static_cast<bufSt*>(calloc(req.count, sizeof(bufSt)));
    for (size_t i = 0; i < req.count; ++i) {
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
        for (size_t j = 0; j < PLANE_COUNT; ++j) {
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

int dequeue_buffer(int fd, int fb) {
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

int process_frame(int fb, size_t index) {
    if (format == V4L2_PIX_FMT_NV12) {
        cv::Mat yuv_input(height * 1.5, width, CV_8UC1, buffers[index].start[0]);
        cv::UMat yuv = yuv_input.clone().getUMat(cv::ACCESS_READ);
        cv::UMat bgr;
        // La conversion de couleur est automatiquement accélérée par OpenCL si activé
        cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGRA_NV12);
        // Écriture des données converties dans le framebuffer
        bgr.getMat(cv::ACCESS_READ).copyTo(bgr_output);
        lseek(fb, 0, SEEK_SET);
        write(fb, bgr_output.data, bgr_output.cols * bgr_output.rows * bgr_output.channels() * sizeof(unsigned char));
    } else {
        cv::Mat yuv_input(height, width, CV_8UC3, buffers[index].start[0]);
        if (bgr_output.empty() || bgr_output.cols != width || bgr_output.rows != height || bgr_output.type() != CV_8UC4) {
            bgr_output.create(height, width, CV_8UC4); // CV_8UC4 pour BGRA
        }
        cv::cvtColor(yuv_input, bgr_output, cv::COLOR_BGR2BGRA);
        lseek(fb, 0, SEEK_SET);
        write(fb, bgr_output.data, bgr_output.cols * bgr_output.rows * bgr_output.channels() * sizeof(unsigned char));
    }

    return EXIT_SUCCESS;
}

int cleanup(int fd, int fb) {
    for (size_t i = 0; i < BUFFER_COUNT; ++i) {
        for(size_t j = 0; j < PLANE_COUNT; ++j){
            if (munmap(buffers[i].start[j], buffers[i].length[j]) == -1) { // Unmap each plane separately
                perror("munmap");
                return -EXIT_FAILURE;
            }
        }
    }

    if (close(fd) < 0) { // Close the device file descriptor
        perror("close");
        return -EXIT_FAILURE;
    }

    if (close(fb) < 0) { // Close the device file descriptor
        perror("close");
        return -EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void signalHandler( int signum ) {
    loop = false;
}

int main() {
    if(cv::ocl::haveOpenCL()) {
        cv::ocl::setUseOpenCL(true);
        std::cout << "OpenCL is enabled in OpenCV" << std::endl;
    } else {
        std::cout << "OpenCL is not available" << std::endl;
    }
    signal(SIGINT, signalHandler); 

    int fd = open("/dev/video0", O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        std::cerr << "Failed to open video device" << std::endl;
        return 1;
    }
    int fb = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        std::cerr << "Impossible d'ouvrir le framebuffer" << std::endl;
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
    width = fmt.fmt.pix_mp.width;
    height = fmt.fmt.pix_mp.height;
    format = fmt.fmt.pix_mp.pixelformat;
    printf("pixelformat: %s\n", fmt2str(fmt.fmt.pix_mp.pixelformat));
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
    for(int i = 0; i < BUFFER_COUNT; i++) {
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
    while(loop) {
        r = select(fd+1, &fds, NULL, NULL, &tv);
        if(-1 == r){
            perror("Waiting for Frame");
            exit(1);
        }

        ret = dequeue_buffer(fd, fb);
        if (ret < 0) {
            perror("dequeue_buffer");
            break;
        }
        auto start = std::chrono::high_resolution_clock::now();
        process_frame(fb, bufferIndex);
        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        if (ret < 0) {
            perror("process_frame");
            break;
        }
        queue_buffer(fd, bufferIndex);
        if (ret < 0) {
            perror("queue_buffer");
            break;
        }
        long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        printf("%ld\n", microseconds);
    }

    stop_capture(fd);
    if (ret < 0) perror("stop_capture");
    return cleanup(fd, fb);
}

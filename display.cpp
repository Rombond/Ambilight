#include <opencv2/opencv.hpp>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>

int main() {
    // Définition des dimensions et des canaux de l'image YUV420p
    int h = 2160;
    int w = 3840;

    // Lecture du fichier YUV420p
    cv::Mat yuv(w, h * 1.5, CV_8U);
    FILE* file = fopen("output.yuv", "rb");
    if (file == nullptr) {
        std::cerr << "Impossible d'ouvrir le fichier output.yuv" << std::endl;
        return 1;
    }
    fread(yuv.data, w * h * 1.5, 1, file);
    fclose(file);

    // Conversion de l'image YUV dans le format BGR NV12 pour correspondance avec OpenCV
    cv::Mat bgr(h, w, CV_8UC4);
    cv::Mat yuvReshape = yuv.reshape(int(h*1.5), w);
    cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGRA_NV12);

    // Ecrire l'image dans le framebuffer
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        std::cerr << "Impossible d'ouvrir le framebuffer" << std::endl;
        return 1;
    }
    write(fd, bgr.data, bgr.cols * bgr.rows * bgr.channels() * sizeof(unsigned char));
    close(fd);

    return 0;
}

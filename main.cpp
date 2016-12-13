// OpenCV headers
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/video/background_segm.hpp>

// Standard headers
#include <stdio.h>
#include <ctime>

// Project headers
#include "tinyfiledialogs.h"

//What mode the program will run in
enum load_video_type { LOAD_VIDEO_FILE, LOAD_VIDEO_FEED };

enum visual_type {GET_MAG, GET_PHASE, GET_IMG};

std::string type2str(int type)
{
    std::string r;

    uchar depth = type & CV_MAT_DEPTH_MASK;
    uchar chans = 1 + (type >> CV_CN_SHIFT);

    switch (depth)
    {
    case CV_8U:  r = "8U"; break;
    case CV_8S:  r = "8S"; break;
    case CV_16U: r = "16U"; break;
    case CV_16S: r = "16S"; break;
    case CV_32S: r = "32S"; break;
    case CV_32F: r = "32F"; break;
    case CV_64F: r = "64F"; break;
    default:     r = "User"; break;
    }

    r += "C";
    r += (chans + '0');

    return r;
}

void FFT(cv::Mat &input, cv::Mat &output, int flag)
{
    //cv::Mat padded;                            
    //expand input image to optimal size
    //int m = cv::getOptimalDFTSize(input.rows);
    //int n = cv::getOptimalDFTSize(input.cols); 
    // on the border add zero values
    //copyMakeBorder(input, padded, 0, m - input.rows, 0, n - input.cols, cv::BORDER_CONSTANT, cv::Scalar::all(0));

    //Initialising picture in complex coordinates
    if (flag == cv::DFT_COMPLEX_OUTPUT)
    {
        cv::Mat planes[] = { cv::Mat_<float>(input), cv::Mat::zeros(input.size(), CV_32F) };
        cv::Mat complexI;
        merge(planes, 2, complexI);
        //complexI is the image in complex coordinates, the real part is the first channel and the complex part is the second channel.
        dft(complexI, output, flag);
    }
    else
        dft(input, output, flag);
}

cv::VideoCapture load_video(load_video_type type, char const * file_loc = NULL)
{
    cv::VideoCapture cap;
    if (type == LOAD_VIDEO_FILE)
        cap = cv::VideoCapture(file_loc);
    else
    {
        cv::Mat test;
        cap = cv::VideoCapture(0);
        cap.set(CV_CAP_PROP_FPS, 26);
        tinyfd_messageBox("Video check", "Check if your video is working and align the shot how you see fit.\nWhen you are ready press any key to end the calibration.", "ok", "info", 1);
        while (true)
        {
            if (!cap.read(test))
            {
                tinyfd_messageBox("Video check", "Couldn't get video frame.", "ok", "error", 1);
                 return EXIT_FAILURE;
            }
            cv::imshow("Test", test);

            if (cv::waitKey(30) >= 0) break;
        }
        cv::destroyWindow("Test");
    }

    if (!cap.isOpened())
    {
        tinyfd_messageBox("Video check", "Couldn't open video.", "ok", "error", 1);
        return EXIT_FAILURE;
    }

    return cap;
}

void resize_limit(cv::Mat & input, int max_dim)
{
    int new_dim = 0;
    if (input.cols > max_dim)
    {
        new_dim = (int)floorf((float)input.rows * ((float)max_dim / (float)input.cols));
        cv::resize(input, input, cv::Size(max_dim, new_dim));
        resize_limit(input, max_dim);
    }
    else if (input.rows > max_dim)
    {
        new_dim = (int)floorf((float)input.cols * ((float)max_dim / (float)input.rows));
        cv::resize(input, input, cv::Size(new_dim, max_dim));
        resize_limit(input, max_dim);
    }
}

void swap_quads(cv::Mat &source)
{
    // rearrange the quadrants of Fourier image  so that the origin is at the image center
    int cx = source.cols / 2;
    int cy = source.rows / 2;

    cv::Mat q0(source, cv::Rect(0, 0, cx, cy));   // Top-Left - Create a ROI per quadrant
    cv::Mat q1(source, cv::Rect(cx, 0, cx, cy));  // Top-Right
    cv::Mat q2(source, cv::Rect(0, cy, cx, cy));  // Bottom-Left
    cv::Mat q3(source, cv::Rect(cx, cy, cx, cy)); // Bottom-Right

    cv::Mat tmp;                           // swap quadrants (Top-Left with Bottom-Right)
    q0.copyTo(tmp);
    q3.copyTo(q0);
    tmp.copyTo(q3);

    q1.copyTo(tmp);                    // swap quadrant (Top-Right with Bottom-Left)
    q2.copyTo(q1);
    tmp.copyTo(q2);
}

// switch to logarithmic scale
void log_scale(cv::Mat &source)
{
    source += cv::Scalar::all(1);
    cv::log(source, source);
}

void information_extraction(cv::Mat &complexI, cv::Mat &output_mag, cv::Mat &output_phase)
{
    cv::Mat planes[2];
    // compute the magnitude and switch to logarithmic scale
    // => log(1 + sqrt(Re(DFT(I))^2 + Im(DFT(I))^2))
    split(complexI, planes);                   
    
    // planes[0] = Re(DFT(I), planes[1] = Im(DFT(I))
    magnitude(planes[0], planes[1], output_mag);
    
    // planes[0] = magnitude
    phase(planes[0], planes[1], output_phase);

    cv::Mat magI = output_mag;
    magI += cv::Scalar::all(1);                    
    
    // switch to logarithmic scale
    cv::log(magI, magI);

    // crop the spectrum, if it has an odd number of rows or columns
    output_mag = magI(cv::Rect(0, 0, magI.cols & -2, magI.rows & -2));
}

void visualise(cv::Mat &input, char * window_name, int flag)
{
    cv::Mat visual;
    cv::normalize(input, visual, 0, 255, CV_MINMAX, CV_8UC3);
    if (flag != GET_IMG)
    {
        cv::applyColorMap(visual, visual, cv::COLORMAP_JET);
        if (flag == GET_MAG)
            swap_quads(visual);
    }
    cv::imshow(window_name, visual);
}

void frame_analysis(cv::Mat &input, cv::Mat &output)
{
    cv::imshow("Original", input);

    //Forward FFT
    cv::Mat grey;
    cv::cvtColor(input, grey, CV_BGR2GRAY);
    FFT(grey, output, cv::DFT_COMPLEX_OUTPUT);

    //Extract information
    cv::Mat magnitude_spectrum, phase_spectrum;
    information_extraction(output, magnitude_spectrum, phase_spectrum);

    //Visualise FFT
    visualise(magnitude_spectrum, "Magnitude", GET_MAG);
    visualise(phase_spectrum, "Angle", GET_PHASE);

    //Inverse FFT
    cv::Mat reverse_components[2];
    FFT(output, input, cv::DFT_REAL_OUTPUT);

    //I don't know why I need to flip in x and y axis... Majid I have failed you...
    cv::flip(input, input, -1);
    split(input, reverse_components);
    visualise(reverse_components[0], "Reverse", GET_IMG);
}

void video_loop(cv::VideoCapture &video_source)
{
    cv::Mat frame, output;

    int fps_of_video = (int)video_source.get(cv::CAP_PROP_FPS);
    clock_t begin_sec = clock(), begin = clock(), end = clock();
    unsigned int count = 0;
    float FPS = 0;
    int wait = (CLOCKS_PER_SEC / fps_of_video);
    std::cout << "Video show a frame every " << wait << " milliseconds (" << fps_of_video << " FPS)" << std::endl;

    while (true)
    {
        if ((end - begin) / wait >= 1)
        {
            if ((end - begin_sec) / CLOCKS_PER_SEC >= 1)
            {
                FPS = (float)count;
                count = 0;
                begin_sec = clock();
            }
            begin = clock();

            if (!video_source.read(frame))
                break;

            resize_limit(frame, 720);
            cv::putText(frame, std::to_string(FPS), cv::Point(10, frame.rows - 40), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar::all(255), 2, 8);
            frame_analysis(frame, output);

            //Need to get frame rate correct, involving the waitkey
            if (cv::waitKey(1) >= 0) break;
            count++;
        }
        end = clock();
    }
}

int main()
{
    //comment
    bool resp = tinyfd_messageBox("Input type", "Do you want to use your primary webcam as the input source?\nSelecting 'No' brings up a file dialog.", "yesno", "question", 0);

    cv::VideoCapture video_source;

    if (resp)
    {
        video_source = load_video(LOAD_VIDEO_FEED);
    }
    else
    {
        char const * lFilterPatterns[2] = { "*.mp4", "*.avi" };
        char const * file_open = tinyfd_openFileDialog("Choose your video file", "", 2, lFilterPatterns, "Video files (.mp4, .avi)", false);
        video_source = load_video(LOAD_VIDEO_FILE, file_open);
    }

    video_loop(video_source);
    return EXIT_SUCCESS;
}
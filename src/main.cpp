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
#include "include/tinyfiledialogs/tinyfiledialogs.h"

//What mode the program will run in
enum load_video_type { LOAD_VIDEO_FILE, LOAD_VIDEO_FEED };

enum visual_type {GET_MAG, GET_PHASE, GET_IMG};

void FFT(cv::Mat &input, cv::Mat &output, int flag)
{
    //Initialising picture in complex coordinates
    if (flag == cv::DFT_COMPLEX_OUTPUT)
    {
        cv::Mat planes[] = { cv::Mat_<float>(input), cv::Mat::zeros(input.size(), CV_32F) };
        cv::Mat complexI;
        cv::merge(planes, 2, complexI);
        //complexI is the image in complex coordinates, the real part is the first channel and the complex part is the second channel.
        cv::dft(complexI, output, flag);
    }
    else
        cv::dft(input, output, flag);
}

bool load_video(cv::VideoCapture &load_into, load_video_type type, char const * file_loc = NULL)
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
                return false;
            }
            cv::imshow("Test", test);

            if (cv::waitKey(30) >= 0) break;
        }
        cv::destroyWindow("Test");
    }

    if (!cap.isOpened())
    {
        tinyfd_messageBox("Video check", "Couldn't open video.", "ok", "error", 1);
        return false;
    }

    load_into = cap;
    return true;
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

void information_extraction(cv::Mat &complexI, cv::Mat &output_mag, cv::Mat &output_phase)
{
    cv::Mat planes[2];
    // compute the magnitude and switch to logarithmic scale
    // => log(1 + sqrt(Re(DFT(I))^2 + Im(DFT(I))^2))
    cv::split(complexI, planes);                   

    // planes[0] = Real(DFT(I), planes[1] = Imaginary(DFT(I))
    cv::cartToPolar(planes[0], planes[1], output_mag, output_phase);

    //Add one to all values
    cv::Mat magI = output_mag;
    magI += cv::Scalar::all(1);                    
    
    // switch to logarithmic scale
    cv::log(magI, magI);

    // crop the spectrum, if it has an odd number of rows or columns
    //output_mag = magI(cv::Rect(0, 0, magI.cols & -2, magI.rows & -2));
}

void information_injection(cv::Mat &input_mag, cv::Mat &input_phase, cv::Mat &complexI)
{
    //input_mag = input_mag(cv::Rect(0, 0, input_mag.cols & -2, input_mag.rows & -2));
    //input_phase = input_phase(cv::Rect(0, 0, input_mag.cols & -2, input_mag.rows & -2));

    cv::exp(input_mag, input_mag);

    input_mag -= cv::Scalar::all(1);

    cv::Mat real;
    cv::Mat imaginary;

    cv::polarToCart(input_mag, input_phase, real, imaginary);

    std::vector<cv::Mat> plane_merge;

    plane_merge.push_back(real);
    plane_merge.push_back(imaginary);

    cv::merge(plane_merge, complexI);
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

std::string type2str(int type) {
    std::string r;

    uchar depth = type & CV_MAT_DEPTH_MASK;
    uchar chans = 1 + (type >> CV_CN_SHIFT);

    switch (depth) {
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

bool drawing;

void MouseControl(int event, int x, int y, int flags, void * userdata)
{
    cv::Mat &image = *((cv::Mat*) userdata);
    if (event == cv::EVENT_LBUTTONDOWN)
        drawing = true;
    if(drawing)
    {
        cv::circle(image, cv::Point(x, y), 20, 0, -1);
    }
    if (event == cv::EVENT_LBUTTONUP)
        drawing = false;
    if (event == cv::EVENT_RBUTTONDBLCLK)
        image = cv::Mat::ones(image.size(), CV_8UC1);
}

void frame_analysis(cv::Mat &input, cv::Mat * mask, cv::Mat &output)
{
    cv::imshow("Original", input);

    //Forward FFT
    cv::Mat grey;
    cv::cvtColor(input, grey, CV_BGR2GRAY);
    FFT(grey, output, cv::DFT_COMPLEX_OUTPUT);

    //Extract information
    cv::Mat phase_spectrum, magnitude_spectrum, masked_magnitude_spectrum;
    information_extraction(output, magnitude_spectrum, phase_spectrum);
    
    swap_quads(*mask);
    cv::setMouseCallback("Magnitude", MouseControl, mask);
    magnitude_spectrum.copyTo(masked_magnitude_spectrum, *mask);

    //Visualise FFT
    cv::Mat magnitude_clone = masked_magnitude_spectrum.clone();
    visualise(magnitude_clone, (char*)"Magnitude", GET_MAG);
    cv::Mat phase_spectrum_clone = phase_spectrum.clone();
    visualise(phase_spectrum_clone, (char*)"Angle", GET_PHASE);
    swap_quads(*mask);

    //Inverse FFT
    information_injection(masked_magnitude_spectrum, phase_spectrum, output);
    cv::Mat reverse_components[2];
    FFT(output, input, cv::DFT_REAL_OUTPUT);

    //I don't know why I need to flip in x and y axis... Majid I have failed you...
    cv::flip(input, input, -1);
    cv::split(input, reverse_components);
    visualise(reverse_components[0], "Reverse", GET_IMG);
}

int video_loop(cv::VideoCapture &video_source)
{
    cv::Mat frame, output;

    int fps_of_video = (int)video_source.get(cv::CAP_PROP_FPS);
    clock_t begin_sec = clock(), begin = clock(), end = clock();
    unsigned int count = 0;
    float FPS = 0;
    int wait = (CLOCKS_PER_SEC / fps_of_video);
    std::cout << "Video show a frame every " << wait << " milliseconds (" << fps_of_video << " FPS)" << std::endl;

    cv::Mat * mask = new cv::Mat();
    bool first = true;

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
                return EXIT_FAILURE;

            resize_limit(frame, 360);
            if (first) 
            {
                *mask = cv::Mat::ones(frame.rows, frame.cols, CV_8UC1);
                first = false;
            }

            cv::putText(frame, std::to_string(FPS), cv::Point(10, frame.rows - 40), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar::all(255), 2, 8);
            frame_analysis(frame, mask, output);

            //Using waitkey's delay is not good for precise frame-rates
            if (cv::waitKey(1) >= 0) break;
            count++;
        }
        end = clock();
    }
    delete mask;

    return EXIT_SUCCESS;
}

int main()
{
    int resp = tinyfd_messageBox("Input type", "Do you want to use your primary webcam as the input source?\nSelecting 'No' brings up a file dialog.", "yesno", "question", 0);

    cv::VideoCapture video_source;

    if (resp)
    {
        if(!load_video(video_source, LOAD_VIDEO_FEED))
            return EXIT_FAILURE;
    }
    else
    {
        char const * lFilterPatterns[2] = { "*.mp4", "*.avi" };
        char const * file_open = tinyfd_openFileDialog("Choose your video file", "", 2, lFilterPatterns, "Video files (.mp4, .avi)", false);
        if(!load_video(video_source, LOAD_VIDEO_FILE, file_open))
            return EXIT_FAILURE;
    }

    return video_loop(video_source);
}
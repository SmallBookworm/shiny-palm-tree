//
// Created by peng on 18-3-1.
//


#include "ball_tracker.h"
#include "cv-helpers.hpp"

using namespace std;
using namespace cv;

Tracker::Tracker() {
    this->frameI = 0;
    ringWatcher.ring[0] = -1;
    reboundTest = false;
}

template<typename T>
double Tracker::distance(T x1, T x2, T y1, T y2) {
    return sqrt(pow(x1 - x2, 2) + pow(y1 - y2, 2));
}

double Tracker::realDistance(cv::Vec3f point1, cv::Vec3f point2) {
    return sqrt(pow(point1[0] - point2[0], 2) + pow(point1[1] - point2[1], 2) + pow(point1[2] - point2[2], 2));
}

Mat Tracker::leastSquares(cv::Mat inMat, cv::Mat outMat) {
    Mat res = inMat.t() * inMat;
    res = res.inv();
    res *= inMat.t();
    res *= outMat;
    return res;

}

std::vector<float> Tracker::curveFitting(std::vector<float> x, std::vector<float> y, int dimension) {
    int size = static_cast<int>(x.size());
    Mat inMat(size, dimension + 1, CV_32FC1), outMat(size, 1, CV_32FC1);
    for (int i = 0; i < size; ++i) {
        auto *pxvec = inMat.ptr<float>(i);
        int j;
        for (j = 0; j < dimension + 1; ++j) {
            pxvec[j] = static_cast<float>(pow(x[i], j));
        }
        auto *pyvec = outMat.ptr<float>(i);
        pyvec[0] = y[i];
    }
    Mat res = this->leastSquares(inMat, outMat);
    vector<float> ds;
    for (int k = 0; k < dimension + 1; ++k) {
        ds.push_back(res.at<float>(0, k));
    }
    return ds;
}

Vec3f Tracker::x2curveFitting(std::vector<float> x, std::vector<float> y) {
    int size = static_cast<int>(x.size());
    Mat inMat(size, 3, CV_32FC1), outMat(size, 1, CV_32FC1);
    for (int i = 0; i < size; ++i) {
        auto *pxvec = inMat.ptr<float>(i);
        int j;
        for (j = 0; j < 3; ++j) {
            pxvec[j] = static_cast<float>(pow(x[i], j));
        }
        auto *pyvec = outMat.ptr<float>(i);
        pyvec[0] = y[i];
    }
    Mat res = this->leastSquares(inMat, outMat);
    return Vec3f(res.at<float>(0, 0), res.at<float>(0, 1), res.at<float>(0, 2));
}

vector<vector<Point>> Tracker::findAllContours(Mat &input, bool isDepth) {
    Mat frame = input.clone();
    //expansive working
    Mat element = getStructuringElement(MORPH_RECT, Size(2, 2));
    dilate(frame, frame, element);

    if (!isDepth)
        cvtColor(frame, frame, CV_BGR2GRAY);
    //高斯平滑
    GaussianBlur(frame, frame, Size(9, 9), 0, 0);
//    imshow("GaussianBlur", frame);

    Canny(frame, frame, 30, 60, 3);
    imshow("Canny", frame);

    vector<vector<Point>> contours;
    std::vector<Vec4i> hierarchy;
    cv::findContours(frame, contours, hierarchy,
                     CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, Point(0, 0));
    //drawContours(frame, contours, i, color, 2, 8, hierarchy, 0, Point());

    return contours;
}

std::vector<std::vector<Point>> Tracker::findForegroundContours(
        Mat &img, double scale) {
    resize(img, img, Size(0, 0), scale, scale);

    Mat fgmask, fgimg, bgimg;
    this->pBackgroundKnn->apply(img, fgmask);

    medianBlur(fgmask, fgmask, 5);

    morphologyEx(fgmask, fgmask, MORPH_CLOSE, Mat::ones(15, 3, CV_8UC1));
    //test
//    namedWindow("MORPH_CLOSE", CV_WINDOW_NORMAL);
//    resizeWindow("MORPH_CLOSE", 1080, 720);
    img = fgmask;

    std::vector<std::vector<Point>> region_contours;
    findContours(fgmask, region_contours, CV_RETR_EXTERNAL,
                 CV_CHAIN_APPROX_SIMPLE, Point(0, 0));
    return region_contours;
}

int Tracker::pSum(cv::Mat gray) {
    int counter = 0;
    //迭代器访问像素点
    Mat_<uchar>::iterator it = gray.begin<uchar>();
    Mat_<uchar>::iterator itend = gray.end<uchar>();
    for (; it != itend; ++it) {
        if ((*it) > 0) counter += 1;//二值化后，像素点是0或者255
    }
    return counter;
}

std::vector<RotatedRect> Tracker::getRotatedRect(std::vector<std::vector<Point>> region_contours) {
    std::vector<RotatedRect> objects;
    for (size_t i = 0; i != region_contours.size(); ++i) {
        RotatedRect rect = minAreaRect(region_contours[i]);
        objects.push_back(rect);
    }
    return objects;
}

Vec4f Tracker::getEdgeCircle(std::vector<Point> contour) {
    Vec4f circle;
    Point2f center;
    float radius = 0;
    minEnclosingCircle(contour, center, radius);
    circle[0] = center.x;
    circle[1] = center.y;
    circle[2] = radius;
    circle[3] = 0;
    for (auto &point:contour) {
        circle[3] += abs(sqrt(pow(center.x - point.x, 2) + pow(center.y - point.y, 2)) - radius);
    }
    circle[3] /= contour.size();
    return circle;
}

cv::Vec3f Tracker::getCircleCoordinate(cv::Vec4f circle, cv::Vec3f info, int wWidth, int wHeight) {
    Vec3f coordinate;
    coordinate[2] = info[2];
    coordinate[0] = static_cast<float>(info[2] * tan(HANGLE / 2) * (wWidth / 2 - circle[0]) /
                                       (wWidth / 2));
    coordinate[1] = static_cast<float>(info[2] * tan(VANGLE / 2) * (wHeight / 2 - circle[1]) /
                                       (wHeight / 2));
    //change coordinate system
    float c1=coordinate[1],c2=coordinate[2];
    coordinate[1] = static_cast<float>(cos(SENSEANGLE) * c1 + sin(SENSEANGLE) * c2);
    coordinate[2] = static_cast<float>(cos(SENSEANGLE) * c2 - sin(SENSEANGLE) * c1);
    return coordinate;
}

float Tracker::getCircleDepth(cv::Vec4f circle, rs2::depth_frame depthFrame) {
    float result = 0;
    int count = 0;
    float a = circle[2] / 2;
    if ((circle[0] + circle[2]) >= depthFrame.get_width() || (circle[1] + circle[2]) >= depthFrame.get_height() ||
        (circle[0] - circle[2]) < 0 || (circle[1] - circle[2]) < 0)
        return -1;
    for (int i = static_cast<int>(ceil(circle[1] - a)); i < circle[1] + a; ++i) {
        double squareX = pow(a, 2) - pow(i - circle[1], 2);
        int maxX = static_cast<int>(sqrt(squareX) + circle[0]);
        int minX = static_cast<int>(ceil(circle[0] - sqrt(squareX)));
        for (int j = minX; j < maxX; ++j) {
            float depth = depthFrame.get_distance(j, i);
            if (depth <= 0)
                continue;
            //test
            //cout << "point" << i << "," << j << ":" << depth << endl;
            result += depth;
            count++;
        }
    }
    return result / count;
}


Rect Tracker::selectROIDepth(std::string windowName, cv::Mat &depthMat) {
    Rect rect = selectROI(windowName, depthMat);
    cout << "tl:" << rect.tl() << endl;
    return rect;
}

cv::Vec4f
Tracker::getBall(std::vector<std::vector<cv::Point>> contours, Mat &resultImage, rs2::depth_frame depthFrame) {
    bool minI = false;
    Vec4f minC;
    Vec3f realC;
    float cDepth;
    float cSize;
    float minSizes, maxsizes, minX, minY, maxX, maxY, minDi, maxDi, minZ, maxZ, maxC, minR, maxR;
    minSizes = 10;
    maxsizes = 50;
    if (this->realCoordinates.empty()) {
        //initial region
        maxY = resultImage.rows / 2;
        minY = 0;
        maxX = resultImage.cols;
        minX = 0;
        minZ = 1.000;
        maxZ = 5.000;

        minR = 0.05;
        maxR = 0.5;

        maxC = 10;
    } else {
        minR = 0.05;
        maxR = 0.5;

        Vec3f info = this->ballInfo.back();
        Vec3f realCI = this->realCoordinates.back();
        //speed 50
//        maxX = before[0] + 100 * (this->frameI - info[0]);
//        minX = before[0];
//        maxY = before[1] + 50 * (this->frameI - info[0]);
//        minY = before[1] - 50 * (this->frameI - info[0]);

        maxDi = 2 * (this->frameI - info[0]);
        minDi = 0;
        //make sure that ball goes far away.
        maxZ = realCI[2] + 1.00 * (this->frameI - info[0]) + 1;
        minZ = realCI[2] + 1.00 * (this->frameI - info[0]) - 1;

        minSizes = static_cast<float>(info[1] / (2 * (this->frameI - info[0])));
        Vec4f before = this->ballCoordinates.back();
        maxC = before[3] * 2;
        maxC = 1 > maxC ? 1 : maxC;
    }
    for (auto &contour : contours) {
        //1 point's number
        //cout<<contour.size()<<endl;
        if (contour.size() < minSizes || contour.size() > maxsizes)
            continue;

        Vec4f circle = this->getEdgeCircle(contour);
        //2 grade of circle
        if (circle[3] > maxC)
            continue;
        cout << circle << endl;

        float depth = this->getCircleDepth(circle, depthFrame);
        if (isnan(depth) || depth < 0)
            continue;
        cout << "depth:" << depth << endl;

        //3 ball radius is not changed
        //Of course,ball's radius don't change when coordinate system is changed.
        // Horizontal FOV (HD 16:9): 64; Vertical FOV (HD 16:9): 41
        double realR = circle[2] / (resultImage.cols / 2) * depth * tan(HANGLE / 2);
        //test
        cout << realR << endl;
        if (realR < minR || realR > maxR)
            continue;

        Vec3f coor = this->getCircleCoordinate(circle, Vec3f(0, 0, depth), depthFrame.get_width(),
                                               depthFrame.get_height());

        //test
        cout << coor << endl;
        //judge zone when empty.if not,distance.
        if (this->ballInfo.empty()) {
            //4 initial region
            if (coor[2] > maxZ || coor[2] < minZ)
                continue;
            if (circle[0] > maxX || circle[0] < minX)
                continue;
            if (circle[1] > maxY || circle[1] < minY)
                continue;
        } else {
            double dis = this->realDistance(coor, this->realCoordinates.back());
            cout << "distance:" << dis << endl;
            if (dis > maxDi || dis < minDi)
                continue;
        }


        if (!minI) {
            minC = circle;
            cSize = contour.size();
            cDepth = depth;
            realC = coor;
            minI = true;
        } else if (circle[3] < minC[3]) {
            minC = circle;
            cSize = contour.size();
            cDepth = depth;
            realC = coor;
        }
    }
    if (!minI) {
        minC[0] = -1;
        return minC;
    }
    this->ballCoordinates.push_back(minC);
    this->ballInfo.emplace_back(this->frameI, cSize, cDepth);
    this->realCoordinates.push_back(realC);
    //test
    Point center(round(minC[0]), round(minC[1]));
    int radius = round(minC[2]);
    cv::circle(resultImage, center, radius, Scalar(0, 255, 0), 1);
    cerr << minC << endl;
    cerr << this->ballInfo.back() << endl;
    cerr << realC << endl;
    return minC;
}


int Tracker::passCF() {
    unsigned long size = this->ballInfo.size();
    //when size is 2,fit line.
    if (size == 2) {
        //ball coordinates in ring's plane (x,y,z)
        Vec3f point;
        point[2] = ringWatcher.coordinate[2];
        //x
        vector<float> xs, ys;
        for (int i = 0; i < size; ++i) {
            //x
            xs.push_back(this->realCoordinates[i][0]);
            //z
            ys.push_back(this->realCoordinates[i][2]);
        }
        vector<float> func1 = this->curveFitting(xs, ys, 1);
        point[0] = (point[2] - func1[0]) / func1[1];
        //y
        xs.clear();
        ys.clear();
        for (int i = 0; i < size; ++i) {
            //y
            xs.push_back(this->realCoordinates[i][1]);
            //z
            ys.push_back(this->realCoordinates[i][2]);
        }
        vector<float> func2 = this->curveFitting(xs, ys, 1);
        point[1] = (point[2] - func2[0]) / func2[1];

        double dis = this->realDistance(ringWatcher.coordinate, point);
        //d-value
        Vec3f dv = point - ringWatcher.coordinate;
        this->dValue.x = -dv[0];
        this->dValue.y = dv[1];

        float br = this->ballCoordinates.back()[2];
        float bdepth = this->ballInfo.back()[2];
        //Of course,ball's radius don't change when coordinate system is changed.
        // Horizontal FOV (HD 16:9): 64; Vertical FOV (HD 16:9): 41
        double realR = br / (848 / 2) * bdepth * tan(HANGLE / 2);
        if (realR + dis < ringWatcher.r)
            return 1;
        else if (dis < ringWatcher.r)
            return 3;
        else
            return 2;
    } else if (size > 2) {
        //ball coordinates in ring's plane (x,y,z)
        Vec3f point;
        point[2] = ringWatcher.coordinate[2];
        //x
        vector<float> xs, ys;
        for (int i = 0; i < size; ++i) {
            //x
            xs.push_back(this->realCoordinates[i][0]);
            //z
            ys.push_back(this->realCoordinates[i][2]);
        }
        vector<float> func1 = this->curveFitting(xs, ys, 1);
        point[0] = (point[2] - func1[0]) / func1[1];
        //y
        float b = func1[1];
        //1/sin(a)
        double bc = sqrt(pow(1 / b, 2) + 1);
        xs.clear();
        ys.clear();
        for (int j = 0; j < size; ++j) {
            xs.push_back(static_cast<float &&>(this->realCoordinates[j][2] * bc));
            ys.push_back(this->realCoordinates[j][1]);
        }
        Vec3f func2 = this->x2curveFitting(xs, ys);
        point[1] = static_cast<float>(func2[0] + func2[1] * point[2] * bc + func2[2] * pow(point[2] * bc, 2));

        double dis = this->realDistance(ringWatcher.coordinate, point);
        //d-value
        Vec3f dv = point - ringWatcher.coordinate;
        this->dValue.x = -dv[0];
        this->dValue.y = dv[1];

        float br = this->ballCoordinates.back()[2];
        float bdepth = this->ballInfo.back()[2];
        //Of course,ball's radius don't change when coordinate system is changed.
        // Horizontal FOV (HD 16:9): 64; Vertical FOV (HD 16:9): 41
        double realR = br / (848 / 2) * bdepth * tan(HANGLE / 2);
        if (realR + dis < ringWatcher.r)
            return 1;
        else if (dis < ringWatcher.r)
            return 3;
        else
            return 2;
    } else {
        //restart
        this->clearInfo();
        return -1;
    }
}

int Tracker::isPassed(cv::Mat &frame, rs2::depth_frame depthFrame) {
    Mat forground = frame.clone();
    vector<vector<Point>> contours = this->findForegroundContours(forground, 1);
    //test
    imshow("MORPH_CLOSE", forground);
    if(frameI<30)
        imwrite("/home/peng/文档/test/f"+to_string(frameI)+".jpg", forground);
    //debouncing
    double sum = forground.cols * forground.rows;
    if ((pSum(forground) / sum) > 0.5)
        return -1;
//test ring-wather
//    Mat clo = frame.clone();
//    ringWatcher.getRing(clo);
//    imshow("fuck?", clo);

    Mat result = frame.clone();
    Vec4f circle = getBall(contours, result, depthFrame);
    // get result or restart when no ball in 5 frames
    if (circle[0] < 0) {
        int res = -1;
        if (!this->ballInfo.empty() && this->frameI - this->ballInfo.back()[0] > 4) {
            res = this->passCF();
        }
        return res;
    }
    //test
    namedWindow("ball", WINDOW_AUTOSIZE);
    //resizeWindow("ball", 848, 480);
    imshow("ball", result);
    //usleep(100000);
    //judge result when ball passed ring's plane
    Vec3f info0 = this->realCoordinates.back();
    if (info0[2] >= ringWatcher.coordinate[2]) {
        int res = this->passCF();
        return res;
    }

    return 0;
}

cv::Vec4f Tracker::getReBall(std::vector<std::vector<cv::Point>> contours, cv::Mat &resultImage,
                             rs2::depth_frame depthFrame) {
    bool minI = false;
    Vec4f minC;
    Vec3f realC;
    float cDepth;
    float cSize;
    float minSizes, maxsizes, minX, minY, maxX, maxY, minDi, maxDi, minZ, maxZ, maxC, minR, maxR;
    minSizes = 10;
    maxsizes = 50;
    if (this->reBall.empty()) {

        Vec3f info = this->realCoordinates.back();
        maxDi = 1.5;
        minDi = 0;
        maxZ = info[2] + 1;
        minZ = info[2] - 1;
        Vec4f before = this->ballCoordinates.back();
        minR = before[2] / 4;
        maxR = before[2] * 3;
    } else {
        Vec4f info = this->reBall.back();

        maxDi = 1.5;
        minDi = 0;
        maxZ = info[2] + 1;
        minZ = info[2] - 1;
        minR = info[3] / 4;
        maxR = info[3] * 3;
    }
    for (auto &contour : contours) {
        //cout<<contour.size()<<endl;
        if (contour.size() < minSizes || contour.size() > maxsizes)
            continue;

        Vec4f circle = this->getEdgeCircle(contour);
        //cout << circle << endl;
        if (circle[2] < minR || circle[2] > maxR)
            continue;

        float depth = this->getCircleDepth(circle, depthFrame);
        if (isnan(depth) || depth < 0)
            continue;
        Vec3f coor = this->getCircleCoordinate(circle, Vec3f(0, 0, depth), depthFrame.get_width(),
                                               depthFrame.get_height());
        if (coor[2] > maxZ || coor[2] < minZ)
            continue;
        //test
        cout << "depth:" << depth << endl;
        cout << circle << endl;
        //judge zone when empty.if not,distance.
        if (this->reBall.empty()) {
            double dis = this->realDistance(coor, this->realCoordinates.back());
            cout << "distance:" << dis << endl;
            if (dis > maxDi || dis < minDi)
                continue;
        } else {
            Vec4f before = this->reBall.back();
            Vec3f coordinate(before[0], before[1], before[2]);
            double dis = this->realDistance(coor, coordinate);
            cout << "distance:" << dis << endl;
            if (dis > maxDi || dis < minDi)
                continue;
        }


        if (!minI) {
            minC = circle;
            cSize = contour.size();
            cDepth = depth;
            realC = coor;
            minI = true;
        } else if (circle[3] < minC[3]) {
            minC = circle;
            cSize = contour.size();
            cDepth = depth;
            realC = coor;
        }
    }
    if (!minI) {
        minC[0] = -1;
        return minC;
    }
    this->reBall.push_back(Vec4f(realC[0], realC[1], realC[2], minC[2]));
    this->reBallInfo.emplace_back(this->frameI, cSize, cDepth);
    //test
    Point center(round(minC[0]), round(minC[1]));
    int radius = round(minC[2]);
    cv::circle(resultImage, center, radius, Scalar(0, 255, 0), 1);
    cerr << minC << endl;
    cerr << realC << endl;
    return minC;
}

int Tracker::surePassed(cv::Mat &frame, rs2::depth_frame depthFrame) {
    vector<vector<Point>> contours = this->findForegroundContours(frame, 1);
    Mat result = frame.clone();
    Vec4f circle = getReBall(contours, result, depthFrame);
    //test
    namedWindow("ball", WINDOW_AUTOSIZE);
    //resizeWindow("ball", 848, 480);
    imshow("ball", result);
    //usleep(100000);

    //judge result when ball passed 5 frames
    if (this->frameI - this->ballInfo.back()[0] > 4) {
        if (this->reBall.size() < 2) {
            this->clearInfo();
            return 1;
        } else {
            float dDep = 0;
            dDep = this->realCoordinates.back()[2] - this->reBall.front()[2];
            for (int i = 1; i < this->reBall.size(); ++i) {
                if (dDep < 0) {
                    this->clearInfo();
                    return 1;
                }
            }
            //fail when  ball closes in 5 frames
            this->clearInfo();
            return 2;
        }
    }

    if (circle[0] < 0) {
        return -1;
    } else
        return 0;
}

void Tracker::clearInfo() {
    reboundTest = false;
    this->ballInfo.clear();
    this->ballCoordinates.clear();
    this->realCoordinates.clear();
    this->reBall.clear();
    this->reBallInfo.clear();
}

int Tracker::test() {
//    // Declare depth colorizer for pretty visualization of depth data
//    rs2::colorizer color_map;
//
//    // Declare RealSense pipeline, encapsulating the actual device and sensors
//    rs2::pipeline pipe;
//    //Create a configuration for configuring the pipeline with a non default profile
//    rs2::config cfg;
//    //Add desired streams to configuration
//    cfg.enable_stream(RS2_STREAM_INFRARED, 1280, 720, RS2_FORMAT_Y8, 30);
//    cfg.enable_stream(RS2_STREAM_DEPTH, 1280, 720, RS2_FORMAT_Z16, 30);
//    //cfg.enable_stream(RS2_STREAM_COLOR, 848, 480, RS2_FORMAT_BGR8, 30);
//    // Start streaming with default recommended configuration
//    pipe.start(cfg);
//
//    const auto window_name = "Display Image";
//    namedWindow(window_name, WINDOW_AUTOSIZE);
//    bool contFlag = true;
//    while (contFlag) {
//        rs2::frameset data = pipe.wait_for_frames(); // Wait for next set of frames from the camera
//        rs2::depth_frame depthFrame = data.get_depth_frame();
//        Mat depthMat = depth_frame_to_meters(pipe, depthFrame);
//        inRange(depthMat, 5.5, 6.3, depthMat);
//        ++this->frameI;
//        cout << "frame:" << this->frameI << endl;
//        //compute result
//        // Update the window with new data
//        imshow(window_name, depthMat);
//        contFlag = waitKey(1) < 0;
//    }
//
//    return EXIT_SUCCESS;
    ringWatcher.ring = Vec4f(384, 103, 60, 5.334);
    ringWatcher.coordinate = this->getCircleCoordinate(ringWatcher.ring, Vec3f(0, 0, ringWatcher.ring[3]),
                                                       848, 480);
    //calculate radius ,it is wrong when camera doesn't look at the front horizontally.In fact,it is known.
    ringWatcher.r = static_cast<float>(ringWatcher.ring[2] / (848 / 2) *
                                       ringWatcher.ring[3] *
                                       tan(HANGLE / 2));
    cout << "r:" << ringWatcher.r << endl;
    cout << "coor" << ringWatcher.coordinate << endl;

    Vec3f point;
    point[2] = 5.334;
    vector<float> xs, ys;
    //x
    xs.push_back(0.63);
    xs.push_back(0.73);
    //z
    ys.push_back(4.26);
    ys.push_back(5.4);

    vector<float> func1 = this->curveFitting(xs, ys, 1);
    point[0] = (point[2] - func1[0]) / func1[1];
    cout << point[0] << endl;
    float b = func1[1];
    //1/sin(a)
    double bc = sqrt(pow(1 / b, 2) + 1);
    cout << bc << endl;
    xs.clear();
    ys.clear();
    xs.push_back(static_cast<float &&>(1));
    ys.push_back(2);

    xs.push_back(static_cast<float &&>(0));
    ys.push_back(1);
    xs.push_back(static_cast<float &&>(2));
    ys.push_back(4);
    Vec3f func2 = this->x2curveFitting(xs, ys);
    cout << func2 << endl;
    point[1] = static_cast<float>(func2[0] + func2[1] * point[2] * bc + func2[2] * pow(point[2] * bc, 2));
    cout << point[1] << endl;
    double dis = this->realDistance(ringWatcher.coordinate, point);
    //d-value
    Vec3f dv = point - ringWatcher.coordinate;
    this->dValue.x = -dv[0];
    this->dValue.y = dv[1];
}


// move-constructible function object (i.e., an object whose class defines operator(), including closures and function objects).
int Tracker::operator()(DeviationPosition &position) try {
    // Declare depth colorizer for pretty visualization of depth data
    rs2::colorizer color_map;

    // Declare RealSense pipeline, encapsulating the actual device and sensors
    rs2::pipeline pipe;
    //Create a configuration for configuring the pipeline with a non default profile
    rs2::config cfg;
    //Add desired streams to configuration
    //cfg.enable_stream(RS2_STREAM_INFRARED, 848, 480, RS2_FORMAT_Y8, 90);
    cfg.enable_stream(RS2_STREAM_DEPTH, 848, 480, RS2_FORMAT_Z16, 60);
    cfg.enable_stream(RS2_STREAM_COLOR, 848, 480, RS2_FORMAT_BGR8, 60);
    // Start streaming with default recommended configuration
    pipe.start(cfg);

    const auto window_name = "Display Image";
    namedWindow(window_name, WINDOW_AUTOSIZE);
    bool status = position.getStop();
    while (!status) {
        rs2::frameset data = pipe.wait_for_frames(); // Wait for next set of frames from the camera
        rs2::depth_frame depthFrame = data.get_depth_frame();
        rs2::frame depth = color_map(depthFrame);
        //rs2::frame ir = data.get_infrared_frame();
        ++this->frameI;
        cout << "frame:" << this->frameI << endl;
        // Create OpenCV matrix of size (w,h) from the colorized depth data
        Mat image = frame_to_mat(data.get_color_frame());

        //get ring data
        if (ringWatcher.ring[0] < 0 && this->frameI > 0) {
//        Mat ringR = image.clone();
//        imshow("ring", ringR);
//        Rect rect = this->selectROIDepth("ring", ringR);
//        cout << "rdepth:" << depthFrame.get_distance(rect.tl().x, rect.tl().y) << endl;

            ringWatcher.ring = Vec4f(450, 330, 60, 4.269);
            ringWatcher.coordinate = this->getCircleCoordinate(ringWatcher.ring, Vec3f(0, 0, ringWatcher.ring[3]),
                                                               depthFrame.get_width(), depthFrame.get_height());
            //calculate radius ,it is wrong when camera doesn't look at the front horizontally.In fact,it is known.
            ringWatcher.r = static_cast<float>(ringWatcher.ring[2] / (depthFrame.get_width() / 2) *
                                               ringWatcher.ring[3] *
                                               tan(HANGLE / 2));

            cout << "r:" << ringWatcher.r << endl;
            cout << "coor" << ringWatcher.coordinate << endl;
            ringWatcher.r = 0.4;
        }

        //compute result
        if (reboundTest) {
            int sure = this->surePassed(image, depthFrame);
            //test
            switch (sure) {
                case -1:
                    cerr << "s:no ball!" << endl;
                    break;
                case 0:
                    cout << "\033[33m" << "s:run" << "\033[0m" << endl;
                    break;
                case 1:
                    cout << "\033[32m" << "sure!" << "\033[0m" << endl;
                    position.setPoint(this->dValue, 1);
                    break;
                case 2:
                    cout << "\033[32m" << "s:fail!" << "\033[0m" << endl;
                    position.setPoint(this->dValue, 0);
                    break;
            }
        } else {
            int pas = this->isPassed(image, depthFrame);
            //test
            switch (pas) {
                case -1:
                    cerr << "no ball!" << endl;
                    break;
                case 0:
                    cout << "\033[33m" << "run" << "\033[0m" << endl;
                    break;
                case 1:
                    cout << "\033[32m" << "success!" << "\033[0m" << endl;
                    position.setPoint(this->dValue, 1);
                    cout << dValue << endl;
                    this->clearInfo();
                    break;
                case 2:
                    cout << "\033[32m" << "fail!" << "\033[0m" << endl;
                    position.setPoint(this->dValue, 0);
                    cout << dValue << endl;
                    this->clearInfo();
                    break;
                case 3:
                    cout << "\033[32m" << "no sure!" << "\033[0m" << endl;
                    reboundTest = true;
                    cout << dValue << endl;
                    break;
            }
        }
        // Update the window with new data
        //test
        imshow(window_name, image);
        if(frameI<30)
            imwrite("/home/peng/文档/test/"+to_string(frameI)+".jpg", image);
        if (waitKey(1) == 27)
            break;

        status = position.getStop();
    };

    return EXIT_SUCCESS;
}
catch (const rs2::error &e) {
    std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    "
              << e.what() << std::endl;
    return EXIT_FAILURE;
}
catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}


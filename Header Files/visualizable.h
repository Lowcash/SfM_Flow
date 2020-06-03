#ifndef IVISUALIZABLE_H
#define IVISUALIZABLE_H
#pragma once

#include "pch.h"
#include "tracking.h"

class IVisualizable {
protected:
    std::thread m_visThread;

    boost::mutex m_visMutex; 

    int m_numClouds, m_numCams, m_numPoints;
public:
    virtual void updatePointCloud(const std::list<cv::Vec3d>& points3D, const std::list<cv::Vec3b>& pointsRGB) = 0;

    virtual void addPoints(const std::vector<cv::Vec3d> points3D) = 0;

    virtual void visualize(const std::string windowName, const cv::Size windowSize, const cv::viz::Color backgroundColor) = 0;
};

#endif //IVISUALIZABLE_H
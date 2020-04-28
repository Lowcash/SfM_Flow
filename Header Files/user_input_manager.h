#ifndef USR_INP_MANAGER_H
#define USR_INP_MANAGER_H
#pragma once

#include "pch.h"
#include "camera.h"

class UserInput {
private:
    const float m_maxRange;
public:
    std::vector<cv::Point2f> m_usrClickedPts2D;
    std::vector<cv::Point2f> m_usrPts2D;
    std::vector<cv::Vec3d> m_usrPts3D;
    
    UserInput(const float maxRange)
        : m_maxRange(maxRange) {}

    void recoverPoints(cv::Mat R, cv::Mat t, Camera camera, cv::Mat& imOutUsr) {
        if (!m_usrPts3D.empty()) {
            std::vector<cv::Point2f> pts2D; cv::projectPoints(m_usrPts3D, R, t, camera.K, cv::Mat(), pts2D);

            for (const auto p : pts2D) {
                std::cout << "Point projected to: " << p << "\n";

                cv::circle(imOutUsr, p, 3, CV_RGB(150, 200, 0), cv::FILLED, cv::LINE_AA);
            }
        }
    }

    void addPoints(const std::vector<cv::Vec3d> pts3D) {
        m_usrPts3D.insert(m_usrPts3D.end(), pts3D.begin(), pts3D.end());
    }

    void addPoints(const std::vector<cv::Point2f> prevPts2D, const std::vector<cv::Point2f> currPts2D) {
        std::map<std::pair<float, float>, float> pointsDist;

        for (auto [it, end, idx] = std::tuple{currPts2D.cbegin(), currPts2D.cend(), 0}; it != end; ++it, ++idx) {
            cv::Point2f p = (cv::Point2f)*it;
            
            m_usrPts2D.push_back(p);
        }
    }

    void updatePoints(const std::vector<cv::Point2f> currPts2D, const cv::Rect boundary, const uint offset) {
        std::map<std::pair<float, float>, float> pointsDist;

        for (auto [it, end, idx] = std::tuple{currPts2D.cbegin(), currPts2D.cend(), 0}; it != end; ++it, ++idx) {
            cv::Point2f p = (cv::Point2f)*it;
            
            m_usrPts2D[idx] = currPts2D[idx];
        }

        for (int i = 0, idxCorrection = 0; i < m_usrPts2D.size(); ++i) {
            auto p = m_usrPts2D[i];

            if (p.x < boundary.x + offset || p.y < boundary.y + offset || p.x > boundary.width - offset || p.y > boundary.height - offset) {
                m_usrPts2D.erase(m_usrPts2D.begin() + (i - idxCorrection));

                idxCorrection++;
            } 
        }
    }

    void recoverPoints(cv::Mat& imOutUsr) {
        for (const auto& p : m_usrPts2D) {
            //std::cout << "Point projected to: " << p << "\n";
                
            cv::circle(imOutUsr, p, 3, CV_RGB(150, 200, 0), cv::FILLED, cv::LINE_AA);
        }
    }

    void recoverPoints(cv::Mat& imOutUsr, cv::Mat cameraK, cv::Mat R, cv::Mat t) {
        if (!m_usrPts3D.empty()) {
            cv::Mat recoveredPts;

            cv::projectPoints(m_usrPts3D, R, t, cameraK, cv::Mat(), recoveredPts);

            for (int i = 0; i < recoveredPts.rows; ++i) {
                auto p = recoveredPts.at<cv::Point2d>(i);
                //std::cout << "Point projected to: " << p << "\n";
                    
                cv::circle(imOutUsr, p, 3, CV_RGB(150, 200, 0), cv::FILLED, cv::LINE_AA);
            }
        } 
    }

    void attachPointsToMove(std::vector<cv::Point2f>& points, std::vector<cv::Point2f>& move) {
        if (!points.empty()) {
            move.insert(move.end(), points.begin(), points.end());
        }
    }

    void detachPointsFromMove(std::vector<cv::Point2f>& points, std::vector<cv::Point2f>& move, uint numPtsToDetach) {
        points.insert(points.end(), move.end() - numPtsToDetach, move.end());

        for (int i = 0; i < numPtsToDetach; ++i)
            move.pop_back();
    }

    void detachPointsFromReconstruction(std::vector<cv::Vec3d>& points, std::vector<cv::Vec3d>& reconstPts, std::vector<cv::Vec3b>& reconstRGB, std::vector<bool>& reconstMask, uint numPtsToDetach) {
        points.insert(points.end(), reconstPts.end() - numPtsToDetach, reconstPts.end());

        for (int i = 0; i < numPtsToDetach; ++i) {
            reconstPts.pop_back();
            reconstRGB.pop_back();
            reconstMask.pop_back();
        }
    }
};

struct UserInputDataParams {
private:
    const std::string m_inputWinName;

    
public:
    UserInput* userInput;
    cv::Mat* m_inputImage;
    
    UserInputDataParams(const std::string inputWinName, cv::Mat* inputImage, UserInput* userInput)
        : m_inputWinName(inputWinName) {
        m_inputImage = inputImage;
        this->userInput = userInput;
    }

    std::string getWinName() const { return m_inputWinName; }

    void updateImage(cv::Mat* inputMat) { m_inputImage = inputMat; }

    cv::Mat* getImage() { return m_inputImage; }
};

#endif //USR_INP_MANAGER_H
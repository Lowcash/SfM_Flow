#include "reconstruction.h"

Reconstruction::Reconstruction(const std::string triangulateMethod, const std::string baMethod, const double baMaxRMSE, const float minDistance, const float maxDistance, const float maxProjectionError, const bool useNormalizePts)
    : m_triangulateMethod(triangulateMethod), m_baMethod(baMethod), m_baMaxRMSE(baMaxRMSE), m_minDistance(minDistance), m_maxDistance(maxDistance), m_maxProjectionError(maxProjectionError), m_useNormalizePts(useNormalizePts), m_numOptimizations(0) {}

void Reconstruction::pointsToRGBCloud(CameraParameters camera, cv::Mat imgColor, cv::Matx33d R, cv::Matx31d t, cv::Mat points3D, cv::Mat inputPts2D, std::vector<cv::Vec3d>& cloud3D, std::vector<cv::Vec3b>& cloudRGB, float minDist, float maxDist, float maxProjErr, std::vector<bool>& mask) {
    //  Project 3D points back to image plane for validation
    cv::Mat _pts2D; cv::projectPoints(points3D, cv::Mat(R), cv::Mat(t), camera.K, cv::Mat(), _pts2D);

    cloud3D.clear();
    cloudRGB.clear();
    
    cv::Matx34d cameraPose; composeExtrinsicMat(R, t, cameraPose);
    const cv::Matx34d cameraProjMat = camera.K33d * cameraPose;

    for(size_t i = 0; i < points3D.rows; ++i) {
        const cv::Vec2d _point2D = _pts2D.at<cv::Vec2d>(i);
        const cv::Vec3d point3D = points3D.at<cv::Vec3d>(i);
        const cv::Vec2d point2D = inputPts2D.at<cv::Vec2d>(i);
        const cv::Vec3b imPoint2D = imgColor.at<cv::Vec3b>(cv::Point(point2D));

        const double err = cv::norm(_point2D - point2D);

        cloud3D.push_back( point3D );
        cloudRGB.push_back( imPoint2D );

        // transfer point from world space to camera space
        const cv::Matx31d pCameraSpace = cameraProjMat * cv::Matx41d(
            point3D[0],
            point3D[1],
            point3D[2],
            1.0
        );

        //  set mask to filter bad projected points, points behind camera and points far away from camera
        mask.push_back( err <  maxProjErr && pCameraSpace(2) > minDist && pCameraSpace(2) < maxDist );
        //mask.push_back( err <  maxProjErr );
    }
}

void Reconstruction::triangulateCloud(CameraParameters camera, const std::vector<cv::Point2f> prevPts, const std::vector<cv::Point2f> currPts, const cv::Mat colorImage, std::vector<cv::Vec3d>& points3D, std::vector<cv::Vec3b>& pointsRGB, std::vector<bool>& mask, const cv::Matx34d prevPose, const cv::Matx34d currPose, cv::Matx33d& R, cv::Matx31d& t) {
    if (prevPts.empty() || currPts.empty()) { return; }
    
    cv::Mat _prevPtsN; cv::undistort(prevPts, _prevPtsN, camera.K33d, cv::Mat());
    cv::Mat _currPtsN; cv::undistort(currPts, _currPtsN, camera.K33d, cv::Mat());

    cv::Mat _prevPtsMat, _currPtsMat; 

    if (m_useNormalizePts) {
        pointsToMat(_prevPtsN, _prevPtsMat);
        pointsToMat(_currPtsN, _currPtsMat);
    } else {
        pointsToMat(prevPts, _prevPtsMat);
        pointsToMat(currPts, _currPtsMat);
    }

    cv::Mat _homogPts, _pts3D;

    if (m_triangulateMethod == "DLT") {
        std::vector<cv::Mat> _pts, _projMats;
        _pts.push_back(_prevPtsMat);
        _pts.push_back(_currPtsMat);
        _projMats.push_back(cv::Mat(camera.K33d * prevPose));
        _projMats.push_back(cv::Mat(camera.K33d * currPose));

        //cv::sfm::triangulatePoints(_pts, _projMats, _pts3D); _pts3D = _pts3D.t();
    } else {
        cv::triangulatePoints(camera.K33d * prevPose, camera.K33d * currPose, _prevPtsMat, _currPtsMat, _homogPts);
        cv::convertPointsFromHomogeneous(_homogPts.t(), _pts3D);
    }

    pointsToRGBCloud(camera, colorImage, R, t, _pts3D, _currPtsMat.t(), points3D, pointsRGB, m_minDistance, m_maxDistance, m_maxProjectionError, mask);
}

void Reconstruction::adjustBundle(CameraData& cameraData, PointCloud& pointCloud) {
    std::cout << "Bundle adjustment...\n" << std::flush;

    if (pointCloud.cloudTracks.empty()) {
        std::cout << "Empty cloud -> interrupting bundle adjustment!" << "\n";

        return;
    }

    // parse camera intrinsic parameters
    cv::Matx14d intrinsics4d (
        cameraData.intrinsics->focal.x,
        cameraData.intrinsics->focal.y,
        cameraData.intrinsics->pp.x,
        cameraData.intrinsics->pp.y
    );

    // parse camera extrinsics parameters
    std::vector<cv::Matx16d> extrinsics6d;

    for (auto [c, cEnd, it] = std::tuple{cameraData.extrinsics.cbegin(), cameraData.extrinsics.cend(), 0}; c != cEnd; ++c, ++it) {
        cv::Matx34d cam = (cv::Matx34d)*c;

        if (cam(0, 0) == 0 && cam(1, 1) == 0 && cam(2, 2) == 0) { 
            extrinsics6d.push_back(cv::Matx16d());
            continue; 
        }

        cv::Vec3f t(cam(0, 3), cam(1, 3), cam(2, 3));
        cv::Matx33f R = cam.get_minor<3, 3>(0, 0);
        float angleAxis[3]; ceres::RotationMatrixToAngleAxis<float>(R.t().val, angleAxis);

        extrinsics6d.push_back(cv::Matx16d(
            angleAxis[0],
            angleAxis[1],
            angleAxis[2],
            t(0),
            t(1),
            t(2)
        ));

        cameraData.extrinsicsCounter[it] = 0;
    }

    // make cloud backup to prevent worse optimalization result
    const std::list<cv::Vec3d> _cloudBackUp(pointCloud.cloud3D);

    ceres::Problem problem;

    bool isBlockLocked = false; size_t numPtsAdded = 0;
    for (auto [pMask, pMaskEnd, p, pEnd, pIdx] = std::tuple{pointCloud.cloudMask.begin(), pointCloud.cloudMask.end(), pointCloud.cloudTracks.begin(), pointCloud.cloudTracks.end(), 0}; pMask != pMaskEnd && p != pEnd; ++pMask, ++p, ++pIdx) {
        if (!(bool)*pMask) { continue; }

        for (auto [c, ct, cEnd, ctEnd, tIdx] = std::tuple{p->projKeys.begin(), p->extrinsicsIdxs.begin(), p->projKeys.end(), p->extrinsicsIdxs.end(), 0}; c != cEnd && ct != ctEnd; ++c, ++ct, ++tIdx) {
            cv::Point2f p2d = *c;
            cv::Matx16d* ext = &extrinsics6d[*ct];

            ceres::CostFunction* costFunc = SnavelyReprojectionError::Create(p2d.x, p2d.y);

            // create problem to solve using residual blocks
            // cloud 3D point positions will be updated
            problem.AddResidualBlock(costFunc, NULL, intrinsics4d.val, ext->val, p->ptrPoint3D->val);

            // lock to the first camera to prevent cloud scaling
            // first camera extrinsics will not be updated
            if (!isBlockLocked) {
                problem.SetParameterBlockConstant(ext->val);

                isBlockLocked = true;
            }

            cameraData.extrinsicsCounter[*ct]++;
        }

        pointCloud.cloudUpdates[pIdx]++;
    }

    if (!isBlockLocked) {
        std::cout << "Minimization is not ready, something went wrong! -> skipping process" << "\n";

        return;
    }
    
    // lock to the camera intinsics to prevent cloud scaling
    // camera intinsics will not be updated
    problem.SetParameterBlockConstant(&intrinsics4d(0));

    ceres::Solver::Options options;

    options.linear_solver_type = m_baMethod == "DENSE_SCHUR" ? 
        ceres::LinearSolverType::DENSE_SCHUR : ceres::LinearSolverType::SPARSE_NORMAL_CHOLESKY;

    options.num_threads = std::thread::hardware_concurrency();
    options.max_num_iterations = 150;

    //options.preconditioner_type = ceres::SCHUR_JACOBI;

    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary);
    //std::cout << summary.FullReport() << "\n";
    
    // check minimalization result -> if it is bad, then restore from backup
    if (!summary.IsSolutionUsable()) {
		std::cout << "Bundle Adjustment failed -> recovering from back up!" << "\n";

        pointCloud.cloud3D = _cloudBackUp;

        return;
	} else {
        double initialRMSE = std::sqrt(summary.initial_cost / summary.num_residuals);
        double finalRMSE = std::sqrt(summary.final_cost / summary.num_residuals);

		// Display minimization result stats
		std::cout << std::endl
			<< "Bundle Adjustment statistics (approximated RMSE):\n"
			<< " #views: " << cameraData.numCameras << "\n"
			<< " #num_residuals: " << summary.num_residuals << "\n"
			<< " Initial RMSE: " << initialRMSE << "\n"
			<< " Final RMSE: " << finalRMSE << "\n"
			<< " Time (s): " << summary.total_time_in_seconds << "\n"
			<< std::endl;

        if (finalRMSE > initialRMSE || finalRMSE > m_baMaxRMSE) {
            std::cout << "Bundle Adjustment failed -> recovering from back up!" << "\n";

            pointCloud.cloud3D = _cloudBackUp;

            return;
        }
	}

    // update camera extrinsics parameters
    for (auto [c, cEnd, c6, c6End, it] = std::tuple{cameraData.extrinsics.begin(), cameraData.extrinsics.end(), extrinsics6d.begin(), extrinsics6d.end(), 0}; c != cEnd && c6 != c6End; ++c, ++c6, ++it) {
        cv::Matx34d& cam = (cv::Matx34d&)*c;
        cv::Matx16d& cam6 = (cv::Matx16d&)*c6;

        if (cam(0, 0) == 0 && cam(1, 1) == 0 && cam(2, 2) == 0) { continue; }

        double rotationMat[9] = { 0 };
        ceres::AngleAxisToRotationMatrix(cam6.val, rotationMat);

        for (int row = 0; row < 3; row++) {
            for (int column = 0; column < 3; column++) {
                cam(column, row) = rotationMat[row * 3 + column];
            }
        }

        cam(0, 3) = cam6(3); 
        cam(1, 3) = cam6(4); 
        cam(2, 3) = cam6(5);
    }


    m_numOptimizations++;

    std::cout << "[DONE]\n";
}

void PointCloud::prepareFilterCloud(pcl::PointCloud<pcl::PointXYZ>::Ptr& cloud, std::vector<size_t>& userCloudIdx) {
    for (auto [p3d, p3dEnd, pIdx] = std::tuple{cloud3D.cbegin(), cloud3D.cend(), 0}; p3d != p3dEnd; ++p3d, ++pIdx) {
        if (cloudMask[pIdx]) {
            cloud->push_back(pcl::PointXYZ(
                p3d->val[0],
                p3d->val[1],
                p3d->val[2]
                )
            );

            userCloudIdx.push_back(pIdx);
        }
    }
}

void PointCloud::applyCloudFilter(std::vector<size_t>& userCloudIdx, std::vector<int>& filter) {
    const size_t indicesSize = filter.size();

    for (size_t i = 0; i < indicesSize; ++i) 
        cloudMask[userCloudIdx[filter[i]]] = false;

    m_numActiveCloudPts -= indicesSize;
}

void PointCloud::filterCloud() {
    pcl::PointCloud<pcl::PointXYZ>::Ptr pointCloud(new pcl::PointCloud<pcl::PointXYZ>);
    std::vector<size_t> usedIdx;

    std::vector<int> filterIndices;

    prepareFilterCloud(pointCloud, usedIdx);

    pcl::StatisticalOutlierRemoval<pcl::PointXYZ> statOutRem;
    statOutRem.setInputCloud(pointCloud);
    statOutRem.setStddevMulThresh(m_cSRemThr);
    statOutRem.setNegative(true);
    statOutRem.filter(filterIndices);

    applyCloudFilter(usedIdx, filterIndices);
}
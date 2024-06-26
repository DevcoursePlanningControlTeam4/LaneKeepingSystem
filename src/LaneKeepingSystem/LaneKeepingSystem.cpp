// Copyright (C) 2023 Grepp CO.
// All rights reserved.

/**
 * @file LaneKeepingSystem.cpp
 * @author Jongrok Lee (lrrghdrh@naver.com)
 * @author Jiho Han
 * @author Haeryong Lim
 * @author Chihyeon Lee
 * @brief Lane Keeping System Class source file
 * @version 1.1
 * @date 2023-05-02
 */
#include "LaneKeepingSystem/LaneKeepingSystem.hpp"

namespace Xycar {
template <typename PREC>
LaneKeepingSystem<PREC>::LaneKeepingSystem()
{
    std::string configPath;
    mNodeHandler.getParam("config_path", configPath);
    YAML::Node config = YAML::LoadFile(configPath);

    // Added
    mSpeedPID = std::make_unique<PIDController<PREC>>(config["SPEED_PID"]["P_GAIN"].as<PREC>(), config["SPEED_PID"]["I_GAIN"].as<PREC>(), config["SPEED_PID"]["D_GAIN"].as<PREC>());
    mPID = std::make_unique<PIDController<PREC>>(config["PID"]["P_GAIN"].as<PREC>(), config["PID"]["I_GAIN"].as<PREC>(), config["PID"]["D_GAIN"].as<PREC>());
    mMovingAverage = std::make_unique<MovingAverageFilter<PREC>>(config["MOVING_AVERAGE_FILTER"]["SAMPLE_SIZE"].as<uint32_t>());
    mHoughTransformLaneDetector = std::make_unique<HoughTransformLaneDetector<PREC>>(config);
    // mVehicleModel = std::make_unique<VehicleModel<PREC>>(0, 0, 0);
    setParams(config);

    mPublisher = mNodeHandler.advertise<xycar_msgs::xycar_motor>(mPublishingTopicName, mQueueSize);
    mSubscriber = mNodeHandler.subscribe(mSubscribedTopicName, mQueueSize, &LaneKeepingSystem::imageCallback, this);

    mStanley = std::make_unique<StanleyController<PREC>>(mStanleyGain, mStanleyLookAheadDistance);
}

template <typename PREC>
void LaneKeepingSystem<PREC>::setParams(const YAML::Node& config)
{
    mPublishingTopicName = config["TOPIC"]["PUB_NAME"].as<std::string>();
    std::cout << 1 << std::endl;
    mSubscribedTopicName = config["TOPIC"]["SUB_NAME"].as<std::string>();
    // std::cout << 2 << std::endl;
    mQueueSize = config["TOPIC"]["QUEUE_SIZE"].as<uint32_t>();
    // std::cout << 3 << std::endl;
    mXycarSpeed = config["XYCAR"]["START_SPEED"].as<PREC>();
    // std::cout << 4 << std::endl;
    mXycarMaxSpeed = config["XYCAR"]["MAX_SPEED"].as<PREC>();
    // std::cout << 5 << std::endl;
    mXycarMinSpeed = config["XYCAR"]["MIN_SPEED"].as<PREC>();
    // std::cout << 6 << std::endl;
    mXycarSpeedControlThreshold = config["XYCAR"]["SPEED_CONTROL_THRESHOLD"].as<PREC>();
    // std::cout << 7 << std::endl;
    mAccelerationStep = config["XYCAR"]["ACCELERATION_STEP"].as<PREC>();
    // std::cout << 8 << std::endl;
    mDecelerationStep = config["XYCAR"]["DECELERATION_STEP"].as<PREC>();
    // std::cout << 9 << std::endl;
    mStanleyGain = config["STANLEY"]["K_GAIN"].as<PREC>();
    // std::cout << 10 << std::endl;
    mStanleyLookAheadDistance = config["STANLEY"]["LOOK_AHREAD_DISTANCE"].as<PREC>();
    // std::cout << 11 << std::endl;
    mDebugging = config["DEBUG"].as<bool>();
    // std::cout << 12 << std::endl;
}

template <typename PREC>
void LaneKeepingSystem<PREC>::run()
{
    ros::Rate rate(kFrameRate);
    while (ros::ok())
    {
        ros::spinOnce();
        if (mFrame.empty())
            continue;

        const auto [leftPosisionX, rightPositionX] = mHoughTransformLaneDetector->getLanePosition(mFrame);

        // int32_t estimatedPositionX = static_cast<int32_t>((leftPosisionX + rightPositionX) / 2);
        mMovingAverage->addSample(static_cast<int32_t>((leftPosisionX + rightPositionX) / 2));
        int32_t estimatedPositionX = static_cast<int32_t>(mMovingAverage->getResult());
        int32_t errorFromMid = estimatedPositionX - static_cast<int32_t>(mFrame.cols / 2) + 6;
        // PREC steeringAngle = std::max(static_cast<PREC>(-kXycarSteeringAangleLimit), std::min(static_cast<PREC>(mPID->getControlOutput(errorFromMid)), static_cast<PREC>(kXycarSteeringAangleLimit)));
        mStanley->calculateSteeringAngle(errorFromMid, 0, mXycarSpeed);
        PREC stanleyResult = mStanley->getResult();
        PREC steeringAngle = std::max(static_cast<PREC>(-kXycarSteeringAangleLimit), std::min(static_cast<PREC>(stanleyResult), static_cast<PREC>(kXycarSteeringAangleLimit)));

        speedControl(steeringAngle);
        drive(steeringAngle);

        if (mDebugging)
        {
            // std::cout << "lpos: " << leftPosisionX << ", rpos: " << rightPositionX << ", mpos: " << estimatedPositionX << std::endl;
            mHoughTransformLaneDetector->drawRectangles(leftPosisionX, rightPositionX, estimatedPositionX);
            cv::imshow("Debug", mHoughTransformLaneDetector->getDebugFrame());
            cv::waitKey(1);
        }
    }
}

template <typename PREC>
void LaneKeepingSystem<PREC>::imageCallback(const sensor_msgs::Image& message)
{
    cv::Mat src = cv::Mat(message.height, message.width, CV_8UC3, const_cast<uint8_t*>(&message.data[0]), message.step);
    cv::cvtColor(src, mFrame, cv::COLOR_RGB2BGR);
}


template <typename PREC>
void LaneKeepingSystem<PREC>::speedControl(PREC steeringAngle)
{
    if (std::abs(steeringAngle) > mXycarSpeedControlThreshold)
    {
        mXycarSpeed -= mDecelerationStep;
        mXycarSpeed = std::max(mXycarSpeed, mXycarMinSpeed);
        return;
    }

    mXycarSpeed += mAccelerationStep;
    mXycarSpeed = std::min(mXycarSpeed, mXycarMaxSpeed);
}

//PID Speed control
/*
template <typename PREC>
void LaneKeepingSystem<PREC>::speedControl(PREC steeringAngle)
{
    PREC targetSpeed = mXycarMaxSpeed;

    if (std::abs(steeringAngle) > mXycarSpeedControlThreshold) {
        mXycarSpeed -= mDecelerationStep;
        targetSpeed = std::max(mXycarSpeed, mXycarMinSpeed);
        return;
    }

    PREC speedError = targetSpeed - mXycarSpeed;
    PREC pidOutput = mSpeedPID->getControlOutput(static_cast<int32_t>(speedError));

    mXycarSpeed += pidOutput;
    mXycarSpeed = std::max(std::min(mXycarSpeed, mXycarMaxSpeed), mXycarMinSpeed);
}
*/
template <typename PREC>
void LaneKeepingSystem<PREC>::drive(PREC steeringAngle)
{
    xycar_msgs::xycar_motor motorMessage;

    motorMessage.angle = std::round(steeringAngle);
    // motorMessage.angle = 50;

    std::cout << steeringAngle << "  " << mXycarSpeed << std::endl;
    motorMessage.speed = std::round(mXycarSpeed);

    mPublisher.publish(motorMessage);
}

template class LaneKeepingSystem<float>;
template class LaneKeepingSystem<double>;
} // namespace Xycar

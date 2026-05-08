#ifdef HAVE_OPENCV

#include "pose_estimator_base.h"

PoseEstimatorBase::PoseEstimatorBase(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<PoseResult>();
}

#endif // HAVE_OPENCV

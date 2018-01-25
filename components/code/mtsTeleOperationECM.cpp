/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-    */
/* ex: set filetype=cpp softtabstop=4 shiftwidth=4 tabstop=4 cindent expandtab: */

/*
  Author(s):  Anton Deguet
  Created on: 2016-01-21

  (C) Copyright 2016-2017 Johns Hopkins University (JHU), All Rights Reserved.

--- begin cisst license - do not edit ---

This software is provided "as is" under an open source license, with
no warranty.  The complete license can be found in license.txt and
http://www.cisst.org/cisst/license.txt.

--- end cisst license ---
*/


// system include
#include <iostream>

// cisst
#include <sawIntuitiveResearchKit/mtsTeleOperationECM.h>
#include <cisstMultiTask/mtsInterfaceProvided.h>
#include <cisstMultiTask/mtsInterfaceRequired.h>

#include <cisstParameterTypes/prmForceCartesianSet.h>

CMN_IMPLEMENT_SERVICES_DERIVED_ONEARG(mtsTeleOperationECM, mtsTaskPeriodic, mtsTaskPeriodicConstructorArg);

mtsTeleOperationECM::mtsTeleOperationECM(const std::string & componentName, const double periodInSeconds):
    mtsTaskPeriodic(componentName, periodInSeconds),
    mMTML(0),
    mMTMR(0),
    mECM(0),
    mTeleopState(componentName, "DISABLED")
{
    Init();
}

mtsTeleOperationECM::mtsTeleOperationECM(const mtsTaskPeriodicConstructorArg & arg):
    mtsTaskPeriodic(arg),
    mMTML(0),
    mMTMR(0),
    mECM(0),
    mTeleopState(arg.Name, "DISABLED")
{
    Init();
}

mtsTeleOperationECM::~mtsTeleOperationECM()
{
    if (mMTML) {
        delete mMTML;
    }
    if (mMTMR) {
        delete mMTMR;
    }
    if (mECM) {
        delete mECM;
    }
}

void mtsTeleOperationECM::Init(void)
{
    if (!mMTML) {
        mMTML = new RobotMTM;
    }
    if (!mMTMR) {
        mMTMR = new RobotMTM;
    }
    if (!mECM) {
        mECM = new RobotECM;
    }
}

void mtsTeleOperationECM::Configure(const std::string & CMN_UNUSED(filename))
{
    // configure state machine
    mTeleopState.AddState("SETTING_ARMS_STATE");
    mTeleopState.AddState("ENABLED");
    mTeleopState.AddAllowedDesiredState("DISABLED");
    mTeleopState.AddAllowedDesiredState("ENABLED");

    // state change, to convert to string events for users (Qt, ROS)
    mTeleopState.SetStateChangedCallback(&mtsTeleOperationECM::StateChanged,
                                         this);

    // run for all states
    mTeleopState.SetRunCallback(&mtsTeleOperationECM::RunAllStates,
                                this);
    // disabled
    mTeleopState.SetTransitionCallback("DISABLED",
                                       &mtsTeleOperationECM::TransitionDisabled,
                                       this);

    // setting arms state
    mTeleopState.SetEnterCallback("SETTING_ARMS_STATE",
                                  &mtsTeleOperationECM::EnterSettingArmsState,
                                  this);
    mTeleopState.SetTransitionCallback("SETTING_ARMS_STATE",
                                       &mtsTeleOperationECM::TransitionSettingArmsState,
                                       this);

    // enabled
    mTeleopState.SetEnterCallback("ENABLED",
                                  &mtsTeleOperationECM::EnterEnabled,
                                  this);
    mTeleopState.SetRunCallback("ENABLED",
                                &mtsTeleOperationECM::RunEnabled,
                                this);
    mTeleopState.SetTransitionCallback("ENABLED",
                                       &mtsTeleOperationECM::TransitionEnabled,
                                       this);

    mScale = 0.2;
    mIsClutched = false;

    StateTable.AddData(mMTML->PositionCartesianCurrent, "MTMLCartesianPosition");
    StateTable.AddData(mMTMR->PositionCartesianCurrent, "MTMRCartesianPosition");
    StateTable.AddData(mECM->PositionCartesianCurrent, "ECMCartesianPosition");

    mConfigurationStateTable = new mtsStateTable(100, "Configuration");
    mConfigurationStateTable->SetAutomaticAdvance(false);
    AddStateTable(mConfigurationStateTable);
    mConfigurationStateTable->AddData(mScale, "Scale");
    mConfigurationStateTable->AddData(mRegistrationRotation, "RegistrationRotation");

    mtsInterfaceRequired * interfaceRequired = AddInterfaceRequired("MTML");
    if (interfaceRequired) {
        interfaceRequired->AddFunction("GetPositionCartesian",
                                       mMTML->GetPositionCartesian);
        //        interfaceRequired->AddFunction("GetPositionCartesianDesired",
        //                               mMTML->GetPositionCartesianDesired);
        interfaceRequired->AddFunction("GetVelocityCartesian",
                                       mMTML->GetVelocityCartesian);
        interfaceRequired->AddFunction("SetPositionCartesian",
                                       mMTML->SetPositionCartesian);
        interfaceRequired->AddFunction("GetCurrentState",
                                       mMTML->GetCurrentState);
        interfaceRequired->AddFunction("GetDesiredState",
                                       mMTML->GetDesiredState);
        interfaceRequired->AddFunction("SetDesiredState",
                                       mMTML->SetDesiredState);
        interfaceRequired->AddFunction("LockOrientation",
                                       mMTML->LockOrientation);
        interfaceRequired->AddFunction("UnlockOrientation",
                                       mMTML->UnlockOrientation);
        interfaceRequired->AddFunction("SetWrenchBody",
                                       mMTML->SetWrenchBody);
        interfaceRequired->AddFunction("SetWrenchBodyOrientationAbsolute",
                                       mMTML->SetWrenchBodyOrientationAbsolute);
        interfaceRequired->AddEventHandlerWrite(&mtsTeleOperationECM::MTMLErrorEventHandler,
                                                this, "Error");
    }

    interfaceRequired = AddInterfaceRequired("MTMR");
    if (interfaceRequired) {
        interfaceRequired->AddFunction("GetPositionCartesian",
                                       mMTMR->GetPositionCartesian);
        //        interfaceRequired->AddFunction("GetPositionCartesianDesired",
        //                               mMTMR->GetPositionCartesianDesired);
        interfaceRequired->AddFunction("GetVelocityCartesian",
                                       mMTMR->GetVelocityCartesian);
        interfaceRequired->AddFunction("SetPositionCartesian",
                                       mMTMR->SetPositionCartesian);
        interfaceRequired->AddFunction("GetCurrentState",
                                       mMTMR->GetCurrentState);
        interfaceRequired->AddFunction("GetDesiredState",
                                       mMTMR->GetDesiredState);
        interfaceRequired->AddFunction("SetDesiredState",
                                       mMTMR->SetDesiredState);
        interfaceRequired->AddFunction("LockOrientation",
                                       mMTMR->LockOrientation);
        interfaceRequired->AddFunction("UnlockOrientation",
                                       mMTMR->UnlockOrientation);
        interfaceRequired->AddFunction("SetWrenchBody",
                                       mMTMR->SetWrenchBody);
        interfaceRequired->AddFunction("SetWrenchBodyOrientationAbsolute",
                                       mMTMR->SetWrenchBodyOrientationAbsolute);
        interfaceRequired->AddEventHandlerWrite(&mtsTeleOperationECM::MTMRErrorEventHandler,
                                                this, "Error");
    }

    interfaceRequired = AddInterfaceRequired("ECM");
    if (interfaceRequired) {
        // ECM, use PID desired position to make sure there is no jump when engaging
        interfaceRequired->AddFunction("GetPositionCartesian",
                                       mECM->GetPositionCartesian);
        interfaceRequired->AddFunction("GetStateJointDesired",
                                       mECM->GetStateJointDesired);
        interfaceRequired->AddFunction("SetPositionJoint",
                                       mECM->SetPositionJoint);
        interfaceRequired->AddFunction("GetCurrentState",
                                       mECM->GetCurrentState);
        interfaceRequired->AddFunction("GetDesiredState",
                                       mECM->GetDesiredState);
        interfaceRequired->AddFunction("SetDesiredState",
                                       mECM->SetDesiredState);
        interfaceRequired->AddEventHandlerWrite(&mtsTeleOperationECM::ECMErrorEventHandler,
                                                this, "Error");
    }

    // footpedal events
    interfaceRequired = AddInterfaceRequired("Clutch");
    if (interfaceRequired) {
        interfaceRequired->AddEventHandlerWrite(&mtsTeleOperationECM::ClutchEventHandler, this, "Button");
    }

    mInterface = AddInterfaceProvided("Setting");
    if (mInterface) {
        mInterface->AddMessageEvents();
        // commands
        mInterface->AddCommandReadState(StateTable, StateTable.PeriodStats,
                                        "GetPeriodStatistics"); // mtsIntervalStatistics

        mInterface->AddCommandWrite(&mtsTeleOperationECM::SetDesiredState, this,
                                    "SetDesiredState", std::string("DISABLED"));
        mInterface->AddCommandWrite(&mtsTeleOperationECM::SetScale, this,
                                    "SetScale", 0.5);
        mInterface->AddCommandWrite(&mtsTeleOperationECM::SetRegistrationRotation, this,
                                    "SetRegistrationRotation", vctMatRot3());
        mInterface->AddCommandReadState(*mConfigurationStateTable,
                                        mScale,
                                        "GetScale");
        mInterface->AddCommandReadState(*mConfigurationStateTable,
                                        mRegistrationRotation,
                                        "GetRegistrationRotation");
        mInterface->AddCommandReadState(StateTable,
                                        mMTML->PositionCartesianCurrent,
                                        "GetPositionCartesianMTML");
        mInterface->AddCommandReadState(StateTable,
                                        mMTMR->PositionCartesianCurrent,
                                        "GetPositionCartesianMTMR");
        mInterface->AddCommandReadState(StateTable,
                                        mECM->PositionCartesianCurrent,
                                        "GetPositionCartesianECM");
        // events
        mInterface->AddEventWrite(MessageEvents.DesiredState,
                                  "DesiredState", std::string(""));
        mInterface->AddEventWrite(MessageEvents.CurrentState,
                                  "CurrentState", std::string(""));
        mInterface->AddEventWrite(MessageEvents.Following,
                                  "Following", false);
        // configuration
        mInterface->AddEventWrite(ConfigurationEvents.Scale,
                                  "Scale", 0.5);
    }
}

void mtsTeleOperationECM::Startup(void)
{
    CMN_LOG_CLASS_INIT_VERBOSE << "Startup" << std::endl;
    SetFollowing(false);
}

void mtsTeleOperationECM::Run(void)
{
    ProcessQueuedCommands();
    ProcessQueuedEvents();

    // run based on state
    mTeleopState.Run();
}

void mtsTeleOperationECM::Cleanup(void)
{
    CMN_LOG_CLASS_INIT_VERBOSE << "Cleanup" << std::endl;
}

void mtsTeleOperationECM::StateChanged(void)
{
    const std::string newState = mTeleopState.CurrentState();
    MessageEvents.CurrentState(newState);
    mInterface->SendStatus(this->GetName() + ": current state is " + newState);
}

void mtsTeleOperationECM::RunAllStates(void)
{
    mtsExecutionResult executionResult;

    // get master left Cartesian position/velocity
    executionResult = mMTML->GetPositionCartesian(mMTML->PositionCartesianCurrent);
    if (!executionResult.IsOK()) {
        CMN_LOG_CLASS_RUN_ERROR << "Run: call to MTML.GetPositionCartesian failed \""
                                << executionResult << "\"" << std::endl;
        mInterface->SendError(this->GetName() + ": unable to get cartesian position from master left");
        this->SetDesiredState("DISABLED");
    }
    executionResult = mMTML->GetVelocityCartesian(mMTML->VelocityCartesianCurrent);
    if (!executionResult.IsOK()) {
        CMN_LOG_CLASS_RUN_ERROR << "Run: call to MTML.GetVelocityCartesian failed \""
                                << executionResult << "\"" << std::endl;
        mInterface->SendError(this->GetName() + ": unable to get cartesian velocity from master left");
        this->SetDesiredState("DISABLED");
    }

    // get master right Cartesian position
    executionResult = mMTMR->GetPositionCartesian(mMTMR->PositionCartesianCurrent);
    if (!executionResult.IsOK()) {
        CMN_LOG_CLASS_RUN_ERROR << "Run: call to MTMR.GetPositionCartesian failed \""
                                << executionResult << "\"" << std::endl;
        mInterface->SendError(this->GetName() + ": unable to get cartesian position from master right");
        this->SetDesiredState("DISABLED");
    }
    executionResult = mMTMR->GetVelocityCartesian(mMTMR->VelocityCartesianCurrent);
    if (!executionResult.IsOK()) {
        CMN_LOG_CLASS_RUN_ERROR << "Run: call to MTMR.GetVelocityCartesian failed \""
                                << executionResult << "\"" << std::endl;
        mInterface->SendError(this->GetName() + ": unable to get cartesian velocity from master right");
        this->SetDesiredState("DISABLED");
    }

    // get slave Cartesian position for GUI
    executionResult = mECM->GetPositionCartesian(mECM->PositionCartesianCurrent);
    if (!executionResult.IsOK()) {
        CMN_LOG_CLASS_RUN_ERROR << "Run: call to ECM.GetPositionCartesian failed \""
                                << executionResult << "\"" << std::endl;
        mInterface->SendError(this->GetName() + ": unable to get cartesian position from slave");
        this->SetDesiredState("DISABLED");
    }
    // for motion computation
    executionResult = mECM->GetStateJointDesired(mECM->StateJointDesired);
    if (!executionResult.IsOK()) {
        CMN_LOG_CLASS_RUN_ERROR << "Run: call to ECM.GetStateJointDesired failed \""
                                << executionResult << "\"" << std::endl;
        mInterface->SendError(this->GetName() + ": unable to get joint state from slave");
        this->SetDesiredState("DISABLED");
    }

    // check if anyone wanted to disable anyway
    if ((mTeleopState.DesiredState() == "DISABLED")
        && (mTeleopState.CurrentState() != "DISABLED")) {
        SetFollowing(false);
        mTeleopState.SetCurrentState("DISABLED");
        return;
    }
}

void mtsTeleOperationECM::TransitionDisabled(void)
{
    if (mTeleopState.DesiredState() == "ENABLED") {
        mTeleopState.SetCurrentState("SETTING_ARMS_STATE");
    }
}

void mtsTeleOperationECM::EnterSettingArmsState(void)
{
    // reset timer
    mInStateTimer = StateTable.GetTic();

    // request state if needed
    std::string armState;
    mECM->GetDesiredState(armState);
    if (armState != "READY") {
        mECM->SetDesiredState(std::string("READY"));
    }
    mMTML->GetDesiredState(armState);
    if (armState != "READY") {
        mMTML->SetDesiredState(std::string("READY"));
    }
    mMTMR->GetDesiredState(armState);
    if (armState != "READY") {
        mMTMR->SetDesiredState(std::string("READY"));
    }
}

void mtsTeleOperationECM::TransitionSettingArmsState(void)
{
    // check if anyone wanted to disable anyway
    if (mTeleopState.DesiredState() == "DISABLED") {
        mTeleopState.SetCurrentState("DISABLED");
        return;
    }
    // check state
    std::string ecmState, leftArmState, rightArmState;
    mECM->GetCurrentState(ecmState);
    mMTML->GetCurrentState(leftArmState);
    mMTMR->GetCurrentState(rightArmState);
    if ((ecmState == "READY") &&
        (leftArmState == "READY") &&
        (rightArmState == "READY")) {
        mTeleopState.SetCurrentState("ENABLED");
        return;
    }
    // check timer
    if ((StateTable.GetTic() - mInStateTimer) > 60.0 * cmn_s) {
        mInterface->SendError(this->GetName() + ": timed out while setting up arms state");
        this->SetDesiredState("DISABLED");
    }
}

void mtsTeleOperationECM::EnterEnabled(void)
{
    // set cartesian effort parameters
    mMTML->SetWrenchBodyOrientationAbsolute(true);
    mMTML->LockOrientation(mMTML->PositionCartesianCurrent.Position().Rotation());
    mMTMR->SetWrenchBodyOrientationAbsolute(true);
    mMTMR->LockOrientation(mMTMR->PositionCartesianCurrent.Position().Rotation());

    // initial state for MTM force feedback
    // -1- initial distance between left and right masters
    vct3 vectorLR;
    vectorLR.DifferenceOf(mMTMR->PositionCartesianCurrent.Position().Translation(),
                          mMTML->PositionCartesianCurrent.Position().Translation());
    mInitial.dLR = vectorLR.Norm();
    // -2- mid-point, aka center of image
    mInitial.C.SumOf(mMTMR->PositionCartesianCurrent.Position().Translation(),
                     mMTML->PositionCartesianCurrent.Position().Translation());
    mInitial.C.Multiply(0.5);
    // -3- image up vector
    mInitial.Up.CrossProductOf(vectorLR, mInitial.C);
    mInitial.Up.NormalizedSelf();
    // -4- width of image, depth of arms wrt image plan
    vct3 side;
    side.CrossProductOf(mInitial.C, mInitial.Up);
    side.NormalizedSelf();
    mInitial.w = 0.5 * vctDotProduct(side, vectorLR);
    mInitial.d = 0.5 * vctDotProduct(mInitial.C.Normalized(), vectorLR);

    // projections
    vct3 normXZ(0.0, 1.0, 0.0); vct3 normYZ(1.0, 0.0, 0.0); vct3 normXY(0.0, 0.0, 1.0);
    mInitial.N.CrossProductOf(mInitial.Up, side);
    mInitial.N.NormalizedSelf();
    mInitial.nXZ = mInitial.N - vctDotProduct(mInitial.N, normXZ)*normXZ;
    mInitial.nXZ.NormalizedSelf();
    mInitial.nYZ = mInitial.N - vctDotProduct(mInitial.N,normYZ)*normYZ;
    mInitial.nYZ.NormalizedSelf();
    mInitial.uXY = mInitial.Up - vctDotProduct(mInitial.Up,normXY)*normXY;
    mInitial.uXY.NormalizedSelf();

    // -5- compute MTMs frame
    mInitial.Frame.Translation().Assign(mInitial.C);
    mInitial.Frame.Rotation().Row(0).Assign(side);
    mInitial.Frame.Rotation().Row(1).Assign(mInitial.Up);
    mInitial.Frame.Rotation().Row(2).Assign(vctCrossProduct(side, mInitial.Up));

#if 1
    std::cerr << CMN_LOG_DETAILS << std::endl
              << "L: " << mMTML->PositionCartesianCurrent.Position().Translation() << std::endl
              << "R: " << mMTMR->PositionCartesianCurrent.Position().Translation() << std::endl
              << "C:  " << mInitial.C << std::endl
              << "Up: " << mInitial.Up << std::endl
              << "d:  " << mInitial.dLR << std::endl
              << "w:  " << mInitial.w << std::endl
              << "d:  " << mInitial.d << std::endl
              << "Si: " << side << std::endl
              << "F:  " << std::endl << mInitial.Frame << std::endl;
#endif
    mECM->PositionJointInitial = mECM->StateJointDesired.Position();
    // check if by any chance the clutch pedal is pressed
    if (mIsClutched) {
        Clutch(true);
    } else {
        SetFollowing(true);
    }
}

void mtsTeleOperationECM::RunEnabled(void)
{
    if (mIsClutched) {
        return;
    }

    const vct3 frictionForceCoeff(-10.0, -10.0, -10.0);
    const double distanceForceCoeff = 150.0;
    static int counter = 0;

    //-1- vector between left and right masters
    vct3 vectorLR;
    vectorLR.DifferenceOf(mMTMR->PositionCartesianCurrent.Position().Translation(),
                          mMTML->PositionCartesianCurrent.Position().Translation());
    // -2- mid-point, aka center of image
    vct3 c;
    c.SumOf(mMTMR->PositionCartesianCurrent.Position().Translation(),
            mMTML->PositionCartesianCurrent.Position().Translation());
    c.Multiply(0.5);
    vct3 directionC = c.Normalized();
    // -3- image up vector
    vct3 up;
    up.CrossProductOf(vectorLR, c);
    up.NormalizedSelf();
    // -4- Width of image
    vct3 side;
    side.CrossProductOf(c, up);
    side.NormalizedSelf();
    // normal to image
    vct3 n;
    n.CrossProductOf(up, side);
    n.NormalizedSelf();
    // -5- find desired position for L and R
    vct3 goalL(c);
    goalL.AddProductOf(-mInitial.w, side);
    goalL.AddProductOf(-mInitial.d, directionC);
    vct3 goalR(c);
    goalR.AddProductOf(mInitial.w, side);
    goalR.AddProductOf(mInitial.d, directionC);

    // compute forces on L and R based on error in position
    vct3 forceFriction;
    vct3 force;
    prmForceCartesianSet wrenchR, wrenchL;

    // MTMR
    // apply force
    force.DifferenceOf(goalR,
                       mMTMR->PositionCartesianCurrent.Position().Translation());
    force.Multiply(distanceForceCoeff);
    wrenchR.Force().Ref<3>(0).Assign(force);
    // add friction force
    forceFriction.ElementwiseProductOf(frictionForceCoeff,
                                       mMTMR->VelocityCartesianCurrent.VelocityLinear());
    wrenchR.Force().Ref<3>(0).Add(forceFriction);
    // apply
    mMTMR->SetWrenchBody(wrenchR);

    // MTML
    // apply force
    force.DifferenceOf(goalL,
                       mMTML->PositionCartesianCurrent.Position().Translation());
    force.Multiply(distanceForceCoeff);
    wrenchL.Force().Ref<3>(0).Assign(force);
    // add friction force
    forceFriction.ElementwiseProductOf(frictionForceCoeff,
                                       mMTML->VelocityCartesianCurrent.VelocityLinear());
    wrenchL.Force().Ref<3>(0).Add(forceFriction);
    // apply
    mMTML->SetWrenchBody(wrenchL);

   // New ECM position
    vctVec goal(mECM->PositionJointInitial);
    vctVec goalJoints(mECM->PositionJointInitial);

    // New Joint positions
    vctVec changeJoints(4);
    vct3 normXZ(0.0, 1.0, 0.0); vct3 normYZ(1.0, 0.0, 0.0); vct3 normXY(0.0, 0.0, 1.0);
    vct3 crossUp, crossN;
    vct3 nXZ, nYZ, uXY;
    
    // - Joint 0 -left/right
    nXZ = n - vctDotProduct(n,normXZ)*normXZ;
    nXZ.NormalizedSelf();
    changeJoints[0] = acos(vctDotProduct(mInitial.nXZ, nXZ));
    crossN = vctCrossProduct(mInitial.nXZ, nXZ);
    if (vctDotProduct(normXZ, crossN) < 0 )
       changeJoints[0] = -changeJoints[0];
    // - Joint 1 - up/down
    nYZ = n - vctDotProduct(n,normYZ)*normYZ;
    nYZ.NormalizedSelf();
    changeJoints[1] = -acos(vctDotProduct(mInitial.nYZ, nYZ));
    crossN = vctCrossProduct(mInitial.nYZ, nYZ);
    if (vctDotProduct(normYZ, crossN) < 0 )
       changeJoints[1] = -changeJoints[1];
    // - Joint 2 - in/out movement
    changeJoints[2] =mInitial.C.Norm() - c.Norm();
    // - Joint 3 - cc/ccw movement
    uXY = up - vctDotProduct(up,normXY)*normXY;
    uXY.NormalizedSelf();
    changeJoints[3] = acos(vctDotProduct(mInitial.uXY, uXY));
    crossN = vctCrossProduct(mInitial.uXY, uXY);
    if (vctDotProduct(normXY, crossN) < 0 )
       changeJoints[3] = -changeJoints[3];
    
    goalJoints.Add(changeJoints);
    mECM->PositionJointSet.Goal().ForceAssign(goalJoints);
    mECM->SetPositionJoint(mECM->PositionJointSet);

if (counter%250 == 0)
    std::cerr << CMN_LOG_DETAILS << std::endl
              << "c: " << c << std::endl <<"mInitialC: "<< mInitial.C << std::endl
              << "goal Joints: " << goalJoints <<std::endl
                     << "change Joints: " << changeJoints << std::endl;
//<< "new Joints: " << newJoints << std::endl;
// else
    // mECM->PositionJointInitial = mECM->StateJointDesired.Position();

    counter++;

}

void mtsTeleOperationECM::TransitionEnabled(void)
{
    std::string armState;

    // check ECM state
    mECM->GetCurrentState(armState);
    if (armState != "READY") {
        mInterface->SendWarning(this->GetName() + ": ECM state has changed to [" + armState + "]");
        mTeleopState.SetDesiredState("DISABLED");
    }

    // check mtml state
    mMTML->GetCurrentState(armState);
    if (armState != "READY") {
        mInterface->SendWarning(this->GetName() + ": MTML state has changed to [" + armState + "]");
        mTeleopState.SetDesiredState("DISABLED");
    }

    // check mtmr state
    mMTMR->GetCurrentState(armState);
    if (armState != "READY") {
        mInterface->SendWarning(this->GetName() + ": MTMR state has changed to [" + armState + "]");
        mTeleopState.SetDesiredState("DISABLED");
    }

    if (mTeleopState.DesiredStateIsNotCurrent()) {
        SetFollowing(false);
        mTeleopState.SetCurrentState(mTeleopState.DesiredState());
    }
}

void mtsTeleOperationECM::SetFollowing(const bool following)
{
    MessageEvents.Following(following);
    mIsFollowing = following;
}

void mtsTeleOperationECM::MTMLErrorEventHandler(const mtsMessage & message)
{
    this->SetDesiredState("DISABLED");
    mInterface->SendError(this->GetName() + ": received from left master [" + message.Message + "]");
}

void mtsTeleOperationECM::MTMRErrorEventHandler(const mtsMessage & message)
{
    this->SetDesiredState("DISABLED");
    mInterface->SendError(this->GetName() + ": received from right master [" + message.Message + "]");
}

void mtsTeleOperationECM::ECMErrorEventHandler(const mtsMessage & message)
{
    this->SetDesiredState("DISABLED");
    mInterface->SendError(this->GetName() + ": received from slave [" + message.Message + "]");
}

void mtsTeleOperationECM::ClutchEventHandler(const prmEventButton & button)
{
    if (button.Type() == prmEventButton::PRESSED) {
        mIsClutched = true;
    } else {
        mIsClutched = false;
    }

    // if the teleoperation is activated
    if (mTeleopState.DesiredState() == "ENABLED") {
        Clutch(mIsClutched);
    }
}

void mtsTeleOperationECM::Clutch(const bool & clutch)
{
    // if the teleoperation is activated
    if (clutch) {
        SetFollowing(false);
        mInterface->SendStatus(this->GetName() + ": console clutch pressed");

        // set MTMs in effort mode, no force applied but gravity and locked orientation
        prmForceCartesianSet wrench;
        mMTML->SetWrenchBody(wrench);
        mMTML->SetGravityCompensation(true);
        mMTML->LockOrientation(mMTML->PositionCartesianCurrent.Position().Rotation());
        mMTMR->SetWrenchBody(wrench);
        mMTMR->SetGravityCompensation(true);
        mMTMR->LockOrientation(mMTMR->PositionCartesianCurrent.Position().Rotation());
    } else {
        mIsClutched = false;
        mInterface->SendStatus(this->GetName() + ": console clutch released");
        mTeleopState.SetCurrentState("SETTING_ARMS_STATE");
    }
}

void mtsTeleOperationECM::SetDesiredState(const std::string & state)
{
    // try to find the state in state machine
    if (!mTeleopState.StateExists(state)) {
        mInterface->SendError(this->GetName() + ": unsupported state " + state);
        return;
    }
    // try to set the desired state
    try {
        mTeleopState.SetDesiredState(state);
    } catch (...) {
        mInterface->SendError(this->GetName() + ": " + state + " is not an allowed desired state");
        return;
    }
    MessageEvents.DesiredState(state);
    mInterface->SendStatus(this->GetName() + ": set desired state to " + state);
}

void mtsTeleOperationECM::SetScale(const double & scale)
{
    mConfigurationStateTable->Start();
    mScale = scale;
    mConfigurationStateTable->Advance();
    ConfigurationEvents.Scale(mScale);
}

void mtsTeleOperationECM::SetRegistrationRotation(const vctMatRot3 & rotation)
{
    mConfigurationStateTable->Start();
    mRegistrationRotation = rotation;
    mConfigurationStateTable->Advance();
}

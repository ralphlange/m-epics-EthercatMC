/*
  FILENAME... ethercatmcIndexerAxis.cpp
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>

#include <epicsThread.h>

#include "motor.h"
#include "ethercatmcController.h"
#include "ethercatmcIndexerAxis.h"

#ifndef ASYN_TRACE_INFO
#define ASYN_TRACE_INFO      0x0040
#endif

#ifndef ASYN_TRACE_DEBUG
#define ASYN_TRACE_DEBUG     0x0080
#endif


typedef enum {
  idxStatusCodeRESET    = 0,
  idxStatusCodeIDLE     = 1,
  idxStatusCodePOWEROFF = 2,
  idxStatusCodeWARN     = 3,
  idxStatusCodeERR4     = 4,
  idxStatusCodeSTART    = 5,
  idxStatusCodeBUSY     = 6,
  idxStatusCodeSTOP     = 7,
  idxStatusCodeERROR    = 8,
  idxStatusCodeERR9     = 9,
  idxStatusCodeERR10    = 10,
  idxStatusCodeERR11    = 11,
  idxStatusCodeERR12    = 12,
  idxStatusCodeERR13    = 13,
  idxStatusCodeERR14    = 14,
  idxStatusCodeERR15    = 15
} idxStatusCodeType;

extern "C" const char *idxStatusCodeTypeToStr(idxStatusCodeType idxStatusCode)
{
  switch (idxStatusCode) {
  case  idxStatusCodeRESET:    return "RESET";
  case  idxStatusCodeIDLE:     return "IDLE ";
  case  idxStatusCodePOWEROFF: return "PWROF";
  case  idxStatusCodeWARN:     return "WARN ";
  case  idxStatusCodeERR4:     return "ERR4 ";
  case  idxStatusCodeSTART:    return "START";
  case  idxStatusCodeBUSY:     return "BUSY ";
  case  idxStatusCodeSTOP:     return "STOP ";
  case  idxStatusCodeERROR:    return "ERROR";
  case  idxStatusCodeERR9:     return "ERR9 ";
  case  idxStatusCodeERR10:    return "ERR10";
  case  idxStatusCodeERR11:    return "ERR11";
  case  idxStatusCodeERR12:    return "ERR12";
  case  idxStatusCodeERR13:    return "ERR13";
  case  idxStatusCodeERR14:    return "ERR14";
  case  idxStatusCodeERR15:    return "ERR15";
  default:                     return "UKNWN";
  }
}


//
// These are the ethercatmcIndexerAxis methods
//

/** Creates a new ethercatmcIndexerAxis object.
 * \param[in] pC Pointer to the ethercatmcController to which this axis belongs.
 * \param[in] axisNo Index number of this axis, range 1 to pC->numAxes_.
 * (0 is not used)
 *
 *
 * Initializes register numbers, etc.
 */
ethercatmcIndexerAxis::ethercatmcIndexerAxis(ethercatmcController *pC,
                                             int axisNo,
                                             int axisFlags,
                                             const char *axisOptionsStr)
  : asynMotorAxis(pC, axisNo),
    pC_(pC)
{
#ifdef motorFlagsDriverUsesEGUString
  setIntegerParam(pC_->motorFlagsDriverUsesEGU_,1);
#endif
#ifdef motorFlagsAdjAfterHomedString
  setIntegerParam(pC_->motorFlagsAdjAfterHomed_, 1);
#endif
  memset(&drvlocal, 0, sizeof(drvlocal));
  memset(&drvlocal.dirty, 0xFF, sizeof(drvlocal.dirty));
  /* We pretend to have an encoder (fActPosition) */
  setIntegerParam(pC_->motorStatusHasEncoder_, 1);
#ifdef motorFlagsNoStopProblemString
  setIntegerParam(pC_->motorFlagsNoStopProblem_, 1);
#endif
#ifdef motorFlagsNoStopOnLsString
  setIntegerParam(pC_->motorFlagsNoStopOnLS_, 1);
#endif
#ifdef motorFlagsLSrampDownString
  setIntegerParam(pC_->motorFlagsLSrampDown_, 1);
#endif
#ifdef motorFlagsPwrWaitForOnString
  setIntegerParam(pC_->motorFlagsPwrWaitForOn_, 1);
#endif

#ifdef motorShowPowerOffString
  setIntegerParam(pC_->motorShowPowerOff_, 1);
#endif
  setStringParam(pC_->ethercatmcreason25_, "Dynamic problem, timeout");
  setStringParam(pC_->ethercatmcreason24_, "Static problem, inhibit");

  /* Set the module name to "" if we have FILE/LINE enabled by asyn */
  if (pasynTrace->getTraceInfoMask(pC_->pasynUserController_) &
      ASYN_TRACEINFO_SOURCE) {
    modNamEMC = "";
  }
  if (axisOptionsStr && axisOptionsStr[0]) {
    printf("Error options not supported %s not found\n", axisOptionsStr);
  }
}

extern "C" int ethercatmcCreateIndexerAxis(const char *ethercatmcName,
                                           int axisNo,
                                           int axisFlags,
                                           const char *axisOptionsStr)
{
  ethercatmcController *pC;
  /* parameters not used */
  (void)axisFlags;
  (void)axisOptionsStr;

  pC = (ethercatmcController*) findAsynPortDriver(ethercatmcName);
  if (!pC) {
    printf("Error port %s not found\n", ethercatmcName);
    return asynError;
  }
  /* The indexer may have created an axis himself, when polling the controller.
     However, if there is a network problem, ADS does not work
     or any other reason that the indexer can not reach the controller,
     create one here to have the records showing "Communication Error" */
  pC->lock();
  if (!pC->getAxis(axisNo)) {
    new ethercatmcIndexerAxis(pC, axisNo, axisFlags, axisOptionsStr);
  }
  pC->unlock();
  return asynSuccess;
}

void ethercatmcIndexerAxis::setIndexerDevNumOffsetTypeCode(unsigned devNum,
                                                           unsigned iOffset,
                                                           unsigned iTypCode)
{
  asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
            "%sOffsetTypeCode(%d) devNum=%u iTypCode=0x%x, iOffset=%u\n",
            modNamEMC, axisNo_, devNum, iTypCode, iOffset);
  drvlocal.devNum = devNum;
  drvlocal.iTypCode = iTypCode;
  drvlocal.iOffset = iOffset;
  if ((drvlocal.iTypCode == 0x5008) || (drvlocal.iTypCode == 0x500c)) {
    drvlocal.lenInPlcPara = 4;
    drvlocal.paramIfOffset = drvlocal.iOffset + 0xA;
  } else if (drvlocal.iTypCode == 0x5010) {
    drvlocal.lenInPlcPara = 8;
    drvlocal.paramIfOffset = drvlocal.iOffset + 22;
  } else {
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
              "%s(%d) iTypCode=0x%x\n",
              modNamEMC, axisNo_, drvlocal.iTypCode);
  }
}

void ethercatmcIndexerAxis::setAuxBitsNotHomedMask(unsigned auxBitsNotHomedMask)
{
  drvlocal.auxBitsNotHomedMask = auxBitsNotHomedMask;
}

void ethercatmcIndexerAxis::addPollNowParam(uint8_t paramIndex)
{
  size_t pollNowIdx;
  const size_t pollNowIdxMax = sizeof(drvlocal.pollNowParams)/sizeof(drvlocal.pollNowParams[0]) - 1;
  for (pollNowIdx = 0; pollNowIdx < pollNowIdxMax; pollNowIdx++) {
    if (!drvlocal.pollNowParams[pollNowIdx]) {
      drvlocal.pollNowParams[pollNowIdx] = paramIndex;
      asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
                "%saddPollNowParam(%d) paramIndex=%s (%d) at pollNowIdx=%u\n",
                modNamEMC, axisNo_,
                plcParamIndexTxtFromParamIndex(paramIndex), paramIndex,
                (unsigned)pollNowIdx);
      return;
    }
  }
}

/** Reports on status of the axis
 * \param[in] fp The file pointer on which report information will be written
 * \param[in] level The level of report detail desired
 *
 * After printing device-specific information calls asynMotorAxis::report()
 */
void ethercatmcIndexerAxis::report(FILE *fp, int level)
{
  if (level > 0) {
    fprintf(fp, "  axis %d\n", axisNo_);
  }

  // Call the base class method
  asynMotorAxis::report(fp, level);
}




/** Move the axis to a position, either absolute or relative
 * \param[in] position in steps
 * \param[in] relative (0=absolute, otherwise relative)
 * \param[in] minimum velocity, steps/sec
 * \param[in] maximum velocity, steps/sec
 * \param[in] acceleration,  steps/sec/sec
 *
 */
asynStatus ethercatmcIndexerAxis::move(double position, int relative,
                                       double minVelocity, double maxVelocity,
                                       double acceleration)
{
  asynStatus status = asynSuccess;
  unsigned traceMask = ASYN_TRACE_INFO;

  asynPrint(pC_->pasynUserController_, traceMask,
            "%smove (%d) position=%f relative=%d minVelocity=%f maxVelocity=%f"
            " acceleration=%f\n",
            "ethercatmcIndexerAxis", axisNo_,
            position, relative, minVelocity, maxVelocity, acceleration);

  if (maxVelocity > 0.0) {
    double oldValue;
    pC_->getDoubleParam(axisNo_, pC_->ethercatmcVel_RB_, &oldValue);
    if (maxVelocity != oldValue) {
      status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                      PARAM_IDX_SPEED_FLOAT,
                                      drvlocal.lenInPlcPara,
                                      maxVelocity);
      if (status) {
        asynPrint(pC_->pasynUserController_, ASYN_TRACE_ERROR,
                  "%smove (%d) status=%s (%d)\n",
                  "ethercatmcIndexerAxis", axisNo_,
                  ethercatmcstrStatus(status), (int)status);
        return status;
      }
      setDoubleParam(pC_->ethercatmcVel_RB_, maxVelocity);
    }
  }
  if (acceleration > 0.0) {
    double oldValue;
    pC_->getDoubleParam(axisNo_, pC_->ethercatmcAcc_RB_, &oldValue);
    if (acceleration != oldValue) {
      status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                      PARAM_IDX_ACCEL_FLOAT,
                                      drvlocal.lenInPlcPara,
                                      acceleration);
      if (status) {
        asynPrint(pC_->pasynUserController_, ASYN_TRACE_ERROR,
                  "%smove (%d) status=%s (%d)\n",
                  "ethercatmcIndexerAxis", axisNo_,
                  ethercatmcstrStatus(status), (int)status);
        return status;
      }
      setDoubleParam(pC_->ethercatmcAcc_RB_, acceleration);
    }
  }
  if (relative && pC_->ctrlLocal.specialDbgStrToMcuDeviceOffset) {
    /* Talking to the simulatar. test case 141 needs a "real"
       moveRelative to be commanded to the controller. */
    char buf[40];
    memset(buf, 0, sizeof(buf));
    snprintf(buf, sizeof(buf) -1,
             "Sim.this.moveRelative=%f",
             position);
    return setStringParamDbgStrToMcu(buf);
  } else if (relative){
    double actPosition;
    pC_->getDoubleParam(axisNo_, pC_->motorPosition_, &actPosition);
    position = position - actPosition;
  }
  if ((drvlocal.iTypCode == 0x5008) || (drvlocal.iTypCode == 0x500c)) {
    unsigned cmdReason = idxStatusCodeSTART  << 12;
    struct {
      uint8_t  posRaw[4];
      uint8_t  cmdReason[2];
    } posCmd;
    doubleToNet(position, &posCmd.posRaw, sizeof(posCmd.posRaw));
    uintToNet(cmdReason, &posCmd.cmdReason, sizeof(posCmd.cmdReason));
    return pC_->setPlcMemoryViaADS(drvlocal.iOffset + drvlocal.lenInPlcPara,
                                   &posCmd, sizeof(posCmd));
  } else if (drvlocal.iTypCode == 0x5010) {
    unsigned cmdReason = idxStatusCodeSTART  << (12 + 16);
    struct {
      uint8_t  posRaw[8];
      uint8_t  cmdReason[4];
    } posCmd;
    doubleToNet(position, &posCmd.posRaw, sizeof(posCmd.posRaw));
    uintToNet(cmdReason, &posCmd.cmdReason, sizeof(posCmd.cmdReason));
    return pC_->setPlcMemoryViaADS(drvlocal.iOffset + drvlocal.lenInPlcPara,
                                   &posCmd, sizeof(posCmd));
  }
  asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
            "%smove(%d) iTypCode=0x%x\n",
            modNamEMC, axisNo_, drvlocal.iTypCode);
  return asynError;
}


/** Home the motor, search the home position
 * \param[in] minimum velocity, mm/sec
 * \param[in] maximum velocity, mm/sec
 * \param[in] acceleration, seconds to maximum velocity
 * \param[in] forwards (0=backwards, otherwise forwards)
 *
 */
asynStatus ethercatmcIndexerAxis::home(double minVelocity, double maxVelocity,
                                       double acceleration, int forwards)
{
  asynStatus status;
  (void)minVelocity;
  (void)maxVelocity;
  (void)acceleration;
  (void)forwards;
  status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                  PARAM_IDX_FUN_REFERENCE,
                                  drvlocal.lenInPlcPara,
                                  0.0);
  return status;
}


/** jog the the motor, search the home position
 * \param[in] minimum velocity, mm/sec (not used)
 * \param[in] maximum velocity, mm/sec (positive or negative)
 * \param[in] acceleration, seconds to maximum velocity
 *
 */
asynStatus ethercatmcIndexerAxis::moveVelocity(double minVelocity,
                                               double maxVelocity,
                                               double acceleration)
{
  unsigned traceMask = ASYN_TRACE_INFO;
  asynStatus status;
  (void)minVelocity;
  (void)acceleration;

  asynPrint(pC_->pasynUserController_, traceMask,
            "%smoveVelocity (%d) minVelocity=%f maxVelocity=%f"
            " acceleration=%f\n", modNamEMC, axisNo_,
            minVelocity, maxVelocity, acceleration);

  if (acceleration > 0.0) {
    double oldValue;
    pC_->getDoubleParam(axisNo_, pC_->ethercatmcAcc_RB_, &oldValue);
    if (acceleration != oldValue) {
      status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                      PARAM_IDX_ACCEL_FLOAT,
                                      drvlocal.lenInPlcPara,
                                      acceleration);
      if (status) {
        asynPrint(pC_->pasynUserController_, ASYN_TRACE_ERROR,
                  "%smoveVelocity (%d) status=%s (%d)\n",
                  "ethercatmcIndexerAxis", axisNo_,
                  ethercatmcstrStatus(status), (int)status);
        return status;
      }
      setDoubleParam(pC_->ethercatmcAcc_RB_, acceleration);
    }
  }
  status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                  PARAM_IDX_FUN_MOVE_VELOCITY,
                                  drvlocal.lenInPlcPara,
                                  maxVelocity);
  return status;
}


/**
 * See asynMotorAxis::setPosition
 */
asynStatus ethercatmcIndexerAxis::setPosition(double value)
{
  asynStatus status = asynError;
  if (drvlocal.paramIfOffset) {
    status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                    PARAM_IDX_FUN_SET_POSITION,
                                    drvlocal.lenInPlcPara,
                                    value);
  }
  return status;
}

asynStatus ethercatmcIndexerAxis::writeCmdRegisster(unsigned idxStatusCode)
{
  asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
            "%swriteCmdRegisster(%d) idxStatusCode=0x%x\n",
            modNamEMC, axisNo_, idxStatusCode);
  if ((drvlocal.iTypCode == 0x5008) || (drvlocal.iTypCode == 0x500c)) {
    unsigned cmdReason = idxStatusCode << 12;
    struct {
      uint8_t  cmdReason[2];
    } posCmd;
    uintToNet(cmdReason, &posCmd.cmdReason, sizeof(posCmd.cmdReason));
    return pC_->setPlcMemoryViaADS(drvlocal.iOffset + (2 * drvlocal.lenInPlcPara),
                                   &posCmd, sizeof(posCmd));
  } else if (drvlocal.iTypCode == 0x5010) {
    unsigned cmdReason = idxStatusCode << (12 + 16);
    struct {
      uint8_t  cmdReason[4];
    } posCmd;
    uintToNet(cmdReason, &posCmd.cmdReason, sizeof(posCmd.cmdReason));
    return pC_->setPlcMemoryViaADS(drvlocal.iOffset + (2 * drvlocal.lenInPlcPara),
                                   &posCmd, sizeof(posCmd));
  }
  asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
            "%swriteCmdRegisster(%d) iTypCode=0x%x\n",
            modNamEMC, axisNo_, drvlocal.iTypCode);
  return asynError;
}

/** Stop the axis
 *
 */
asynStatus ethercatmcIndexerAxis::stopAxisInternal(const char *function_name,
                                                   double acceleration)
{
  asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
            "%sstopAxisInternal(%d) (%s)\n",
            modNamEMC, axisNo_, function_name);

  return writeCmdRegisster(idxStatusCodeSTOP);
}

/** Stop the axis, called by motor Record
 *
 */
asynStatus ethercatmcIndexerAxis::stop(double acceleration )
{
  //drvlocal.eeAxisWarning = eeAxisWarningNoWarning;
  return stopAxisInternal(__FUNCTION__, acceleration);
}

asynStatus ethercatmcIndexerAxis::setIntegerParamLog(int function,
                                                     int newValue,
                                                     const char *name)
{
  int oldValue;
  asynStatus status = pC_->getIntegerParam(axisNo_, function, &oldValue);
  if (status || (newValue != oldValue)) {
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
              "%spoll(%d) %s=%d\n",
              modNamEMC, axisNo_, name, newValue);
  }
  return setIntegerParam(function, newValue);
}

/** Polls the axis.
 * This function reads the motor position, the limit status, the home status,
 * the moving status,
 * and the drive power-on status.
 * It calls setIntegerParam() and setDoubleParam() for each item that it polls,
 * and then calls callParamCallbacks() at the end.
 * \param[out] moving A flag that is set indicating that the axis is moving
 * (true) or done (false). */
asynStatus ethercatmcIndexerAxis::poll(bool *moving)
{
  asynStatus status = asynSuccess;
  if (drvlocal.iTypCode) {
    unsigned traceMask = ASYN_TRACE_INFO;
    const char *msgTxtFromDriver = NULL;
    double targetPosition = 0.0;
    double actPosition = 0.0;
    double paramValue = 0.0;
    unsigned statusReasonAux, paramCtrl;
    uint16_t errorID = 0xFFFF;
    bool nowMoving = false;
    int powerIsOn = 0;
    int statusValid = 0;
    int hasError = 0;
    idxStatusCodeType idxStatusCode;
    unsigned idxReasonBits = 0;
    unsigned idxAuxBits = 0;
    int pollReadBackInBackGround = 1;
    if (drvlocal.dirty.initialPollNeeded) {
      if (drvlocal.iOffset) {
        status = pC_->indexerReadAxisParameters(this, drvlocal.devNum,
                                                drvlocal.iOffset,
                                                drvlocal.lenInPlcPara);
      } else {
        snprintf(pC_->outString_, sizeof(pC_->outString_),
                 "%sGvl_App.axes_comm[%d].wStatusWord?;"
                 "%sGvl_App.axes_comm[%d].fTargetValue?;"
                 "%sGvl_App.axes_comm[%d].fActualValue?",
                 drvlocal.adsport_str, axisNo_,
                 drvlocal.adsport_str, axisNo_,
                 drvlocal.adsport_str, axisNo_);
        status = pC_->writeReadOnErrorDisconnect();
      }
      if (!status) {
        drvlocal.dirty.initialPollNeeded = 0;
        setIntegerParam(pC_->motorStatusCommsError_, 0);
      }
    }
    pC_->getIntegerParam(axisNo_, pC_->motorStatusPowerOn_, &powerIsOn);

    if ((drvlocal.iTypCode == 0x5008) || (drvlocal.iTypCode == 0x500c)) {
      struct {
        uint8_t   actPos[4];
        uint8_t   targtPos[4];
        uint8_t   statReasAux[2];
        uint8_t   paramCtrl[2];
        uint8_t   paramValue[4];
      } readback;
      uint16_t statusReasonAux16;
      status = pC_->getPlcMemoryFromProcessImage(drvlocal.iOffset,
                                                 &readback,
                                                 sizeof(readback));
      if (status) {
        return status;
      }
      actPosition = netToDouble(&readback.actPos,
                                sizeof(readback.actPos));
      targetPosition = netToDouble(&readback.targtPos,
                                   sizeof(readback.targtPos));
      statusReasonAux16 = netToUint(&readback.statReasAux,
                                    sizeof(readback.statReasAux));
      paramCtrl = netToUint(&readback.paramCtrl,
                            sizeof(readback.paramCtrl));
      paramValue = netToDouble(&readback.paramValue,
                               sizeof(readback.paramValue));
      /* Specific bit positions for 5008 */
      idxStatusCode = (idxStatusCodeType)(statusReasonAux16 >> 12);
      idxReasonBits = (statusReasonAux16 >> 8) & 0x0F;
      idxAuxBits    =  statusReasonAux16  & 0x0FF;

      /* 8 aux bits */
      statusReasonAux = statusReasonAux16 & 0xFF;
      /* 4 reason bits */
      statusReasonAux |= (idxReasonBits << 24);
    } else if (drvlocal.iTypCode == 0x5010) {
      struct {
        uint8_t   actPos[8];
        uint8_t   targtPos[8];
        uint8_t   statReasAux[4];
        uint8_t   errorID[2];
        uint8_t   paramCtrl[2];
        uint8_t   paramValue[8];
      } readback;
      status = pC_->getPlcMemoryFromProcessImage(drvlocal.iOffset,
                                                 &readback,
                                                 sizeof(readback));
      if (status) {
        return status;
      }
      actPosition = netToDouble(&readback.actPos,
                                sizeof(readback.actPos));
      targetPosition = netToDouble(&readback.targtPos,
                                   sizeof(readback.targtPos));
      statusReasonAux = netToUint(&readback.statReasAux,
                                  sizeof(readback.statReasAux));
      paramCtrl = netToUint(&readback.paramCtrl,
                            sizeof(readback.paramCtrl));
      paramValue = netToDouble(&readback.paramValue,
                               sizeof(readback.paramValue));
      /* Specific for 5010 */
      errorID = netToUint(&readback.errorID,
                          sizeof(readback.errorID));
      setIntegerParam(pC_->ethercatmcErrId_, errorID);

      idxStatusCode = (idxStatusCodeType)(statusReasonAux >> 28);
      idxReasonBits = (statusReasonAux >> 24) & 0x0F;
      idxAuxBits    =  statusReasonAux  & 0x0FFFFFF;
    } else {
      asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
                "%spoll(%d) iTypCode=0x%x\n",
                modNamEMC, axisNo_, drvlocal.iTypCode);
      return asynError;
    }
    setDoubleParam(pC_->motorPosition_, actPosition);
    setDoubleParam(pC_->motorEncoderPosition_, actPosition);

    drvlocal.hasProblem = 0;
    setIntegerParam(pC_->ethercatmcStatusCode_, idxStatusCode);
    if ((statusReasonAux != drvlocal.old_statusReasonAux) ||
        (idxAuxBits      != drvlocal.old_idxAuxBits)) {
      if (errorID) {
        asynPrint(pC_->pasynUserController_, traceMask,
                  "%spoll(%d) actPos=%f targetPos=%f statusReasonAux=0x%x (%s) errorID=0x%x\n",
                  modNamEMC, axisNo_,
                  actPosition, targetPosition, statusReasonAux,
                  idxStatusCodeTypeToStr(idxStatusCode), errorID);
      } else {
        asynPrint(pC_->pasynUserController_, traceMask,
                  "%spoll(%d) actPos=%f targetPos=%f statusReasonAux=0x%x (%s)\n",
                  modNamEMC, axisNo_,
                  actPosition, targetPosition, statusReasonAux,
                  idxStatusCodeTypeToStr(idxStatusCode));
      }
      drvlocal.old_statusReasonAux = statusReasonAux;
      drvlocal.old_idxAuxBits      = idxAuxBits;
    }
    switch (idxStatusCode) {
      /* After RESET, START, STOP the bits are not valid */
    case idxStatusCodeIDLE:
    case idxStatusCodeWARN:
      powerIsOn = 1;
      statusValid = 1;
      break;
    case idxStatusCodePOWEROFF:
      statusValid = 1;
      drvlocal.hasProblem = 1;
      powerIsOn = 0;
      break;
    case idxStatusCodeBUSY:
      powerIsOn = 1;
      statusValid = 1;
      nowMoving = true;
      break;
    case idxStatusCodeERROR:
      hasError = 1;
      statusValid = 1;
      drvlocal.hasProblem = 1;
      break;
    case idxStatusCodeRESET:
    case idxStatusCodeSTART:
    case idxStatusCodeSTOP:
      /* temporally states, no more information yet  */
      break;
    default:
      drvlocal.hasProblem = 1;
    }
    if (statusValid) {
      int hls = idxReasonBits & 0x8 ? 1 : 0;
      int lls = idxReasonBits & 0x4 ? 1 : 0;
      setIntegerParamLog(pC_->motorStatusLowLimit_, lls,  "LLS");
      setIntegerParamLog(pC_->motorStatusHighLimit_, hls, "HLS");
      setIntegerParam(pC_->motorStatusMoving_, nowMoving);
      setIntegerParam(pC_->motorStatusDone_, !nowMoving);
      setIntegerParam(pC_->ethercatmcStatusBits_, statusReasonAux);
      setIntegerParam(pC_->ethercatmcErr_, hasError);
      if (drvlocal.auxBitsNotHomedMask) {
        int homed = idxAuxBits & drvlocal.auxBitsNotHomedMask ? 0 : 1;
        setIntegerParamLog(pC_->motorStatusHomed_, homed, "homed");
        if (!homed) {
          drvlocal.hasProblem = 1;
        }
      }
      if (hasError) {
        char sErrorMessage[40];
        const char *errIdString = errStringFromErrId(errorID);
        memset(&sErrorMessage[0], 0, sizeof(sErrorMessage));
        if (errIdString[0]) {
          snprintf(sErrorMessage, sizeof(sErrorMessage)-1, "E: %s %x",
                   errIdString, errorID);
        }  else {
          snprintf(sErrorMessage, sizeof(sErrorMessage)-1,
                   "E: TwinCAT Err %x", errorID);
        }
        msgTxtFromDriver = sErrorMessage;
      }
      updateMsgTxtFromDriver(msgTxtFromDriver);
    }
    *moving = nowMoving;
    setIntegerParam(pC_->ethercatmcStatusCode_, idxStatusCode);
    setIntegerParam(pC_->motorStatusProblem_, drvlocal.hasProblem);
    setIntegerParam(pC_->motorStatusPowerOn_, powerIsOn);


    if ((paramCtrl & PARAM_IF_CMD_MASK) == PARAM_IF_CMD_DONE) {
      unsigned paramIndex = paramCtrl & PARAM_IF_IDX_MASK;
      asynPrint(pC_->pasynUserController_,
                ASYN_TRACE_FLOW,
                "%spoll(%d) paramCtrl=%s (0x%x) paramValue=%f\n",
                modNamEMC, axisNo_,
                plcParamIndexTxtFromParamIndex(paramIndex),
                paramCtrl, paramValue);
      if ((paramCtrl != drvlocal.old_paramCtrl) ||
          (paramValue != drvlocal.old_paramValue)) {
        if (paramIndex < PARAM_IF_IDX_FIRST_FUNCTION) {
          /* Only read real parameters, not functions */
          int initial = 0;
          pC_->parameterFloatReadBack(axisNo_,
                                      initial,
                                      paramIndex,
                                      paramValue);
        }
      }
      drvlocal.old_paramCtrl = paramCtrl;
      drvlocal.old_paramValue = paramValue;
    }

    /* Read back the parameters one by one */
    if (pollReadBackInBackGround && (paramCtrl & PARAM_IF_ACK_MASK)) {
      drvlocal.pollNowIdx++;
      if (!drvlocal.pollNowParams[drvlocal.pollNowIdx]) {
        /* The list is 0 terminated */
        drvlocal.pollNowIdx = 0;
      }
      if (drvlocal.pollNowParams[drvlocal.pollNowIdx]) {
        uint16_t paramIndex = drvlocal.pollNowParams[drvlocal.pollNowIdx];
        uint16_t newParamCtrl = PARAM_IF_CMD_DOREAD + paramIndex;
        asynPrint(pC_->pasynUserController_,
                  ASYN_TRACE_FLOW,
                  "%spollNext(%d) paramCtrl=%s (0x%x) paramValue=%f\n",
                  modNamEMC, axisNo_,
                  plcParamIndexTxtFromParamIndex(paramIndex),
                  paramCtrl, paramValue);
        pC_->setPlcMemoryInteger(drvlocal.paramIfOffset,
                                 newParamCtrl, sizeof(newParamCtrl));
      }
    }
    drvlocal.old_statusReasonAux = statusReasonAux;
    drvlocal.old_idxAuxBits      = idxAuxBits;
    callParamCallbacks();
  }
  return status;
}


asynStatus ethercatmcIndexerAxis::resetAxis(void)
{
  return writeCmdRegisster(idxStatusCodeRESET);
}

/** Set the motor closed loop status
 * \param[in] closedLoop true = close loop, false = open looop. */
asynStatus ethercatmcIndexerAxis::setClosedLoop(bool closedLoop)
{
  double value = closedLoop ? 0.0 : 1.0; /* 1.0 means disable */
  asynStatus status = asynError;

  asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
            "%ssetClosedLoop(%d)=%d\n",  modNamEMC, axisNo_,
            (int)closedLoop);
  if (!value) {
    /* Report off before the poller detects off */
    setIntegerParam(pC_->motorStatusPowerOn_, value);
  }
  if (drvlocal.paramIfOffset) {
    status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                    PARAM_IDX_OPMODE_AUTO_UINT,
                                    drvlocal.lenInPlcPara,
                                    value);
  }
  return status;
}


asynStatus ethercatmcIndexerAxis::setIntegerParam(int function, int value)
{
  asynStatus status = asynSuccess;
  if (function == pC_->motorUpdateStatus_) {
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
              "%ssetIntegerParam(%d motorUpdateStatus_)=%d\n",
              modNamEMC, axisNo_, value);

  } else if (function == pC_->motorStatusCommsError_) {
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_FLOW,
              "%ssetIntegerParam(%d pC_->motorStatusCommsError_)=%d\n",
              modNamEMC, axisNo_, value);
    if (value  && !drvlocal.dirty.initialPollNeeded ) {
      asynPrint(pC_->pasynUserController_, ASYN_TRACE_ERROR,
                "%s Communication error(%d)\n", modNamEMC, axisNo_);
      memset(&drvlocal.dirty, 0xFF, sizeof(drvlocal.dirty));
      drvlocal.dirty.initialPollNeeded = 1;
    }
#ifdef ethercatmcErrRstString
  } else if (function == pC_->ethercatmcErrRst_) {
    if (value) {
      asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
                "%ssetIntegerParam(%d ErrRst_)=%d\n",
                modNamEMC, axisNo_, value);
      /*  We do not want to call the base class */
      return resetAxis();
    }
    /* If someone writes 0 to the field, just ignore it */
    return asynSuccess;
#endif
  } else if (function == pC_->ethercatmcCfgDHLM_En_) {
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
              "%ssetIntegerParam(%d ethercatmcCfgDHLM_En)=%d\n",
              modNamEMC, axisNo_, value);
    if (!value) {
      static const double fABSMAX = 3.0e+38;
      status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                      PARAM_IDX_USR_MAX_FLOAT,
                                      drvlocal.lenInPlcPara,
                                      fABSMAX);
      pC_->udateMotorLimitsRO(axisNo_);
    }
    return status;
  } else if (function == pC_->ethercatmcCfgDLLM_En_) {
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
              "%ssetIntegerParam(%d ethercatmcCfgDLLM_En)=%d\n",
              modNamEMC, axisNo_, value);
    if (!value) {
      static const double fABSMIN = -3.0e+38;
      status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                      PARAM_IDX_USR_MIN_FLOAT,
                                      drvlocal.lenInPlcPara,
                                      fABSMIN);
      pC_->udateMotorLimitsRO(axisNo_);
    }
    return status;
  }
  //Call base class method
  status = asynMotorAxis::setIntegerParam(function, value);
  return status;
}

asynStatus ethercatmcIndexerAxis::setDoubleParam(int function, double value)
{
  asynStatus status;
  if (function == pC_->ethercatmcCfgDHLM_) {
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
              "%ssetDoubleParam(%d ethercatmcCfgDHLM_)=%g\n", modNamEMC, axisNo_, value);
    status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                    PARAM_IDX_USR_MAX_FLOAT,
                                    drvlocal.lenInPlcPara,
                                    value);
    asynMotorAxis::setDoubleParam(function, value);
    pC_->udateMotorLimitsRO(axisNo_);
    return status;
  } else if (function == pC_->ethercatmcCfgDLLM_) {
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
              "%ssetDoubleParam(%d ethercatmcCfgDLLM_)=%g\n", modNamEMC, axisNo_, value);
    status = pC_->indexerParamWrite(axisNo_, drvlocal.paramIfOffset,
                                    PARAM_IDX_USR_MIN_FLOAT,
                                    drvlocal.lenInPlcPara,
                                    value);
    asynMotorAxis::setDoubleParam(function, value);
    pC_->udateMotorLimitsRO(axisNo_);
    return status;
  }

  // Call the base class method
  status = asynMotorAxis::setDoubleParam(function, value);
  return status;

}

asynStatus ethercatmcIndexerAxis::setStringParamDbgStrToMcu(const char *value)
{
  asynStatus status;
  const char * const Main_this_str = "Main.this.";
  const char * const Sim_this_str = "Sim.this.";
  /* The special device structure. */
  struct {
    /* 2 bytes control, 46 payload */
    uint8_t   busyLen[2];
    uint8_t   value[46];
  } netDevice0518interface;
  int valueLen;


  asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
            "%ssetStringParamDbgStrToMcu(%d)=\"%s\"\n",
            modNamEMC, axisNo_, value);
  /* empty strings are not send to the controller */
  if (!value[0]) return asynSuccess;
  /* No special device found */
  if (!pC_->ctrlLocal.specialDbgStrToMcuDeviceOffset) return asynError;

  /* Check the string. E.g. Main.this. and Sim.this. are passed
     as Main.M1 or Sim.M1 */
  if (!strncmp(value, Main_this_str, strlen(Main_this_str))) {
    valueLen = snprintf((char*)&netDevice0518interface.value,
                        sizeof(netDevice0518interface.value),
                        "Main.M%d.%s;\n",
                        axisNo_, value + strlen(Main_this_str));
  }
  /* caput IOC:m1-DbgStrToMCU Sim.this.log=M1.log */
  else if (!strncmp(value, Sim_this_str, strlen(Sim_this_str))) {
    valueLen = snprintf((char*)&netDevice0518interface.value,
                        sizeof(netDevice0518interface.value),
                        "Sim.M%d.%s;\n",
                        axisNo_, value + strlen(Sim_this_str));
  } else {
    /* If we come here, the command was not understood */
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_ERROR,
              "%ssetStringParamDbgStrToMcu (%d) illegal value=\"%s\"\n",
              "ethercatmcIndexerAxis", axisNo_, value);
    return asynError;
  }
  if (valueLen < 0 || (unsigned)valueLen >= sizeof(netDevice0518interface.value)) {
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_ERROR,
              "%ssetStringParamDbgStrToMcu (%d) valueLen=%d value=\"%s\"\n",
              "ethercatmcIndexerAxis", axisNo_,
              valueLen, value);
    return asynError;
  }
  uintToNet(valueLen, &netDevice0518interface.busyLen,
            sizeof(netDevice0518interface.busyLen));
  /* obey the handshake */
  status = pC_->indexerWaitSpecialDeviceIdle(pC_->ctrlLocal.specialDbgStrToMcuDeviceOffset);
  if (status) {
    return status;
  }

  /* TODO2: update the simulator to send the "OK" and read it here */
  return pC_->setPlcMemoryViaADS(pC_->ctrlLocal.specialDbgStrToMcuDeviceOffset,
                                 (char*)&netDevice0518interface,
                                 pC_->ctrlLocal.specialDbgStrToMcuDeviceLength);
}

asynStatus ethercatmcIndexerAxis::setStringParam(int function, const char *value)
{
  if (function == pC_->ethercatmcDbgStrToLog_) {
    asynPrint(pC_->pasynUserController_, ASYN_TRACE_INFO,
              "%ssetStringParamDbgStrToLog(%d)=\"%s\"\n",
              modNamEMC, axisNo_, value);
  } else if (function == pC_->ethercatmcDbgStrToMcu_) {
    return setStringParamDbgStrToMcu(value);
  }
  /* Call base class method */
  return asynMotorAxis::setStringParam(function, value);
}

#ifndef motorMessageTextString
void ethercatmcIndexerAxis::updateMsgTxtFromDriver(const char *value)
{
  if (value && value[0]) {
    setStringParam(pC_->ethercatmcMCUErrMsg_,value);
  } else {
    setStringParam(pC_->ethercatmcMCUErrMsg_, "");
  }
}
#endif

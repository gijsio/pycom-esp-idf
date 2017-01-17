/*
 *  Copyright (c) 2016, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   This file implements the top-level interface to the OpenThread stack.
 */

#define WPP_NAME "openthread.tmh"

#ifdef OPENTHREAD_CONFIG_FILE
#include OPENTHREAD_CONFIG_FILE
#else
#include <openthread-config.h>
#endif

#include <openthread.h>

#include <common/code_utils.hpp>
#include <common/debug.hpp>
#include <common/logging.hpp>
#include <common/message.hpp>
#include <common/new.hpp>
#include <common/settings.hpp>
#include <common/timer.hpp>
#include <crypto/mbedtls.hpp>
#include <net/icmp6.hpp>
#include <net/ip6.hpp>
#include <platform/settings.h>
#include <platform/radio.h>
#include <platform/random.h>
#include <platform/misc.h>
#include <thread/thread_netif.hpp>
#include <thread/thread_uris.hpp>
#include <openthread-instance.h>
#include <coap/coap_server.hpp>

using namespace Thread;

#ifndef OPENTHREAD_MULTIPLE_INSTANCE
static otDEFINE_ALIGNED_VAR(sInstanceRaw, sizeof(otInstance), uint64_t);
otInstance *sInstance = NULL;
#endif

otInstance::otInstance(void) :
    mReceiveIp6DatagramCallback(NULL),
    mReceiveIp6DatagramCallbackContext(NULL),
    mActiveScanCallback(NULL),
    mActiveScanCallbackContext(NULL),
    mEnergyScanCallback(NULL),
    mEnergyScanCallbackContext(NULL),
    mThreadNetif(mIp6)
#if OPENTHREAD_ENABLE_RAW_LINK_API
    , mLinkRaw(*this)
#endif // OPENTHREAD_ENABLE_RAW_LINK_API
#if OPENTHREAD_ENABLE_APPLICATION_COAP
    , mApplicationCoapServer(mIp6.mUdp, OT_DEFAULT_COAP_PORT)
#endif // OPENTHREAD_ENABLE_APPLICATION_COAP
{
}

#ifdef __cplusplus
extern "C" {
#endif

ThreadError otSetDelayTimerMinimal(otInstance *aInstance, uint32_t aDelayTimerMinimal)
{
    return aInstance->mThreadNetif.GetLeader().SetDelayTimerMinimal(aDelayTimerMinimal);
}

uint32_t otGetDelayTimerMinimal(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetLeader().GetDelayTimerMinimal();
}

uint8_t otGetMaxAllowedChildren(otInstance *aInstance)
{
    uint8_t aNumChildren;

    (void)aInstance->mThreadNetif.GetMle().GetChildren(&aNumChildren);

    return aNumChildren;
}

ThreadError otSetMaxAllowedChildren(otInstance *aInstance, uint8_t aMaxChildren)
{
    return aInstance->mThreadNetif.GetMle().SetMaxAllowedChildren(aMaxChildren);
}

uint32_t otGetChildTimeout(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetTimeout();
}

void otSetChildTimeout(otInstance *aInstance, uint32_t aTimeout)
{
    aInstance->mThreadNetif.GetMle().SetTimeout(aTimeout);
}

const uint8_t *otGetExtendedPanId(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMac().GetExtendedPanId();
}

void otSetExtendedPanId(otInstance *aInstance, const uint8_t *aExtendedPanId)
{
    uint8_t mlPrefix[8];

    aInstance->mThreadNetif.GetMac().SetExtendedPanId(aExtendedPanId);

    mlPrefix[0] = 0xfd;
    memcpy(mlPrefix + 1, aExtendedPanId, 5);
    mlPrefix[6] = 0x00;
    mlPrefix[7] = 0x00;
    aInstance->mThreadNetif.GetMle().SetMeshLocalPrefix(mlPrefix);
}

ThreadError otGetLeaderRloc(otInstance *aInstance, otIp6Address *aAddress)
{
    ThreadError error;

    VerifyOrExit(aAddress != NULL, error = kThreadError_InvalidArgs);

    error = aInstance->mThreadNetif.GetMle().GetLeaderAddress(*static_cast<Ip6::Address *>(aAddress));

exit:
    return error;
}

otLinkModeConfig otGetLinkMode(otInstance *aInstance)
{
    otLinkModeConfig config;
    uint8_t mode = aInstance->mThreadNetif.GetMle().GetDeviceMode();

    memset(&config, 0, sizeof(otLinkModeConfig));

    if (mode & Mle::ModeTlv::kModeRxOnWhenIdle)
    {
        config.mRxOnWhenIdle = 1;
    }

    if (mode & Mle::ModeTlv::kModeSecureDataRequest)
    {
        config.mSecureDataRequests = 1;
    }

    if (mode & Mle::ModeTlv::kModeFFD)
    {
        config.mDeviceType = 1;
    }

    if (mode & Mle::ModeTlv::kModeFullNetworkData)
    {
        config.mNetworkData = 1;
    }

    return config;
}

ThreadError otSetLinkMode(otInstance *aInstance, otLinkModeConfig aConfig)
{
    uint8_t mode = 0;

    if (aConfig.mRxOnWhenIdle)
    {
        mode |= Mle::ModeTlv::kModeRxOnWhenIdle;
    }

    if (aConfig.mSecureDataRequests)
    {
        mode |= Mle::ModeTlv::kModeSecureDataRequest;
    }

    if (aConfig.mDeviceType)
    {
        mode |= Mle::ModeTlv::kModeFFD;
    }

    if (aConfig.mNetworkData)
    {
        mode |= Mle::ModeTlv::kModeFullNetworkData;
    }

    return aInstance->mThreadNetif.GetMle().SetDeviceMode(mode);
}

const uint8_t *otGetMasterKey(otInstance *aInstance, uint8_t *aKeyLength)
{
    return aInstance->mThreadNetif.GetKeyManager().GetMasterKey(aKeyLength);
}

ThreadError otSetMasterKey(otInstance *aInstance, const uint8_t *aKey, uint8_t aKeyLength)
{
    return aInstance->mThreadNetif.GetKeyManager().SetMasterKey(aKey, aKeyLength);
}

const otIp6Address *otGetMeshLocalEid(otInstance *aInstance)
{
    return &aInstance->mThreadNetif.GetMle().GetMeshLocal64();
}

const uint8_t *otGetMeshLocalPrefix(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetMeshLocalPrefix();
}

ThreadError otSetMeshLocalPrefix(otInstance *aInstance, const uint8_t *aMeshLocalPrefix)
{
    return aInstance->mThreadNetif.GetMle().SetMeshLocalPrefix(aMeshLocalPrefix);
}

const char *otGetNetworkName(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMac().GetNetworkName();
}

ThreadError otSetNetworkName(otInstance *aInstance, const char *aNetworkName)
{
    return aInstance->mThreadNetif.GetMac().SetNetworkName(aNetworkName);
}

bool otIsRouterRoleEnabled(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().IsRouterRoleEnabled();
}

void otSetRouterRoleEnabled(otInstance *aInstance, bool aEnabled)
{
    aInstance->mThreadNetif.GetMle().SetRouterRoleEnabled(aEnabled);
}

uint8_t otGetLocalLeaderWeight(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetLeaderWeight();
}

void otSetLocalLeaderWeight(otInstance *aInstance, uint8_t aWeight)
{
    aInstance->mThreadNetif.GetMle().SetLeaderWeight(aWeight);
}

uint32_t otGetLocalLeaderPartitionId(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetLeaderPartitionId();
}

void otSetLocalLeaderPartitionId(otInstance *aInstance, uint32_t aPartitionId)
{
    return aInstance->mThreadNetif.GetMle().SetLeaderPartitionId(aPartitionId);
}

uint16_t otGetJoinerUdpPort(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetJoinerRouter().GetJoinerUdpPort();
}

ThreadError otSetJoinerUdpPort(otInstance *aInstance, uint16_t aJoinerUdpPort)
{
    return aInstance->mThreadNetif.GetJoinerRouter().SetJoinerUdpPort(aJoinerUdpPort);
}

uint32_t otGetContextIdReuseDelay(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetNetworkDataLeader().GetContextIdReuseDelay();
}

void otSetContextIdReuseDelay(otInstance *aInstance, uint32_t aDelay)
{
    aInstance->mThreadNetif.GetNetworkDataLeader().SetContextIdReuseDelay(aDelay);
}

uint32_t otGetKeySequenceCounter(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetKeyManager().GetCurrentKeySequence();
}

void otSetKeySequenceCounter(otInstance *aInstance, uint32_t aKeySequenceCounter)
{
    aInstance->mThreadNetif.GetKeyManager().SetCurrentKeySequence(aKeySequenceCounter);
}

uint32_t otGetKeySwitchGuardTime(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetKeyManager().GetKeySwitchGuardTime();
}

void otSetKeySwitchGuardTime(otInstance *aInstance, uint32_t aKeySwitchGuardTime)
{
    aInstance->mThreadNetif.GetKeyManager().SetKeySwitchGuardTime(aKeySwitchGuardTime);
}

uint8_t otGetNetworkIdTimeout(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetNetworkIdTimeout();
}

void otSetNetworkIdTimeout(otInstance *aInstance, uint8_t aTimeout)
{
    aInstance->mThreadNetif.GetMle().SetNetworkIdTimeout((uint8_t)aTimeout);
}

uint8_t otGetRouterUpgradeThreshold(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetRouterUpgradeThreshold();
}

void otSetRouterUpgradeThreshold(otInstance *aInstance, uint8_t aThreshold)
{
    aInstance->mThreadNetif.GetMle().SetRouterUpgradeThreshold(aThreshold);
}

ThreadError otReleaseRouterId(otInstance *aInstance, uint8_t aRouterId)
{
    return aInstance->mThreadNetif.GetMle().ReleaseRouterId(aRouterId);
}

ThreadError otBecomeDetached(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().BecomeDetached();
}

ThreadError otBecomeChild(otInstance *aInstance, otMleAttachFilter aFilter)
{
    return aInstance->mThreadNetif.GetMle().BecomeChild(aFilter);
}

ThreadError otBecomeRouter(otInstance *aInstance)
{
    ThreadError error = kThreadError_InvalidState;

    switch (aInstance->mThreadNetif.GetMle().GetDeviceState())
    {
    case Mle::kDeviceStateDisabled:
    case Mle::kDeviceStateDetached:
        break;

    case Mle::kDeviceStateChild:
        error = aInstance->mThreadNetif.GetMle().BecomeRouter(ThreadStatusTlv::kHaveChildIdRequest);
        break;

    case Mle::kDeviceStateRouter:
    case Mle::kDeviceStateLeader:
        error = kThreadError_None;
        break;
    }

    return error;
}

ThreadError otBecomeLeader(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().BecomeLeader();
}

void otPlatformReset(otInstance *aInstance)
{
    otPlatReset(aInstance);
}

void otFactoryReset(otInstance *aInstance)
{
    otPlatSettingsWipe(aInstance);
    otPlatReset(aInstance);
}

ThreadError otPersistentInfoErase(otInstance *aInstance)
{
    ThreadError error = kThreadError_None;

    VerifyOrExit(otGetDeviceRole(aInstance) == kDeviceRoleDisabled, error = kThreadError_InvalidState);
    otPlatSettingsWipe(aInstance);

exit:
    return error;
}

uint8_t otGetRouterDowngradeThreshold(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetRouterDowngradeThreshold();
}

void otSetRouterDowngradeThreshold(otInstance *aInstance, uint8_t aThreshold)
{
    aInstance->mThreadNetif.GetMle().SetRouterDowngradeThreshold(aThreshold);
}

uint8_t otGetRouterSelectionJitter(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetRouterSelectionJitter();
}

void otSetRouterSelectionJitter(otInstance *aInstance, uint8_t aRouterJitter)
{
    aInstance->mThreadNetif.GetMle().SetRouterSelectionJitter(aRouterJitter);
}

ThreadError otGetChildInfoById(otInstance *aInstance, uint16_t aChildId, otChildInfo *aChildInfo)
{
    ThreadError error = kThreadError_None;

    VerifyOrExit(aChildInfo != NULL, error = kThreadError_InvalidArgs);

    error = aInstance->mThreadNetif.GetMle().GetChildInfoById(aChildId, *aChildInfo);

exit:
    return error;
}

ThreadError otGetChildInfoByIndex(otInstance *aInstance, uint8_t aChildIndex, otChildInfo *aChildInfo)
{
    ThreadError error = kThreadError_None;

    VerifyOrExit(aChildInfo != NULL, error = kThreadError_InvalidArgs);

    error = aInstance->mThreadNetif.GetMle().GetChildInfoByIndex(aChildIndex, *aChildInfo);

exit:
    return error;
}

ThreadError otGetNextNeighborInfo(otInstance *aInstance, otNeighborInfoIterator *aIterator, otNeighborInfo *aInfo)
{
    ThreadError error = kThreadError_None;

    VerifyOrExit((aInfo != NULL) && (aIterator != NULL), error = kThreadError_InvalidArgs);

    error = aInstance->mThreadNetif.GetMle().GetNextNeighborInfo(*aIterator, *aInfo);

exit:
    return error;
}

otDeviceRole otGetDeviceRole(otInstance *aInstance)
{
    otDeviceRole rval = kDeviceRoleDisabled;

    switch (aInstance->mThreadNetif.GetMle().GetDeviceState())
    {
    case Mle::kDeviceStateDisabled:
        rval = kDeviceRoleDisabled;
        break;

    case Mle::kDeviceStateDetached:
        rval = kDeviceRoleDetached;
        break;

    case Mle::kDeviceStateChild:
        rval = kDeviceRoleChild;
        break;

    case Mle::kDeviceStateRouter:
        rval = kDeviceRoleRouter;
        break;

    case Mle::kDeviceStateLeader:
        rval = kDeviceRoleLeader;
        break;
    }

    return rval;
}

ThreadError otGetEidCacheEntry(otInstance *aInstance, uint8_t aIndex, otEidCacheEntry *aEntry)
{
    ThreadError error;

    VerifyOrExit(aEntry != NULL, error = kThreadError_InvalidArgs);
    error = aInstance->mThreadNetif.GetAddressResolver().GetEntry(aIndex, *aEntry);

exit:
    return error;
}

ThreadError otGetLeaderData(otInstance *aInstance, otLeaderData *aLeaderData)
{
    ThreadError error;

    VerifyOrExit(aLeaderData != NULL, error = kThreadError_InvalidArgs);

    error = aInstance->mThreadNetif.GetMle().GetLeaderData(*aLeaderData);

exit:
    return error;
}

uint8_t otGetLeaderRouterId(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetLeaderDataTlv().GetLeaderRouterId();
}

uint8_t otGetLeaderWeight(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetLeaderDataTlv().GetWeighting();
}

uint32_t otGetPartitionId(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetLeaderDataTlv().GetPartitionId();
}

uint16_t otGetRloc16(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetRloc16();
}

uint8_t otGetRouterIdSequence(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().GetRouterIdSequence();
}

ThreadError otGetRouterInfo(otInstance *aInstance, uint16_t aRouterId, otRouterInfo *aRouterInfo)
{
    ThreadError error = kThreadError_None;

    VerifyOrExit(aRouterInfo != NULL, error = kThreadError_InvalidArgs);

    error = aInstance->mThreadNetif.GetMle().GetRouterInfo(aRouterId, *aRouterInfo);

exit:
    return error;
}

ThreadError otGetParentInfo(otInstance *aInstance, otRouterInfo *aParentInfo)
{
    ThreadError error = kThreadError_None;
    Router *parent;

    VerifyOrExit(aParentInfo != NULL, error = kThreadError_InvalidArgs);

    parent = aInstance->mThreadNetif.GetMle().GetParent();
    memcpy(aParentInfo->mExtAddress.m8, parent->mMacAddr.m8, OT_EXT_ADDRESS_SIZE);

    aParentInfo->mRloc16          = parent->mValid.mRloc16;
    aParentInfo->mRouterId        = Mle::Mle::GetRouterId(parent->mValid.mRloc16);
    aParentInfo->mNextHop         = parent->mNextHop;
    aParentInfo->mPathCost        = parent->mCost;
    aParentInfo->mLinkQualityIn   = parent->mLinkInfo.GetLinkQuality(aInstance->mThreadNetif.GetMac().GetNoiseFloor());
    aParentInfo->mLinkQualityOut  = parent->mLinkQualityOut;
    aParentInfo->mAge             = static_cast<uint8_t>(Timer::MsecToSec(Timer::GetNow() - parent->mLastHeard));
    aParentInfo->mAllocated       = parent->mAllocated;
    aParentInfo->mLinkEstablished = parent->mState == Neighbor::kStateValid;

exit:
    return error;
}

ThreadError otGetParentAverageRssi(otInstance *aInstance, int8_t *aParentRssi)
{
    ThreadError error = kThreadError_None;
    Router *parent;

    VerifyOrExit(aParentRssi != NULL, error = kThreadError_InvalidArgs);

    parent = aInstance->mThreadNetif.GetMle().GetParent();
    *aParentRssi = parent->mLinkInfo.GetAverageRss();

    VerifyOrExit(*aParentRssi != LinkQualityInfo::kUnknownRss, error = kThreadError_Failed);

exit:
    return error;
}

ThreadError otSetStateChangedCallback(otInstance *aInstance, otStateChangedCallback aCallback, void *aCallbackContext)
{
    ThreadError error = kThreadError_NoBufs;

    for (size_t i = 0; i < OPENTHREAD_CONFIG_MAX_STATECHANGE_HANDLERS; i++)
    {
        if (aInstance->mNetifCallback[i].IsFree())
        {
            aInstance->mNetifCallback[i].Set(aCallback, aCallbackContext);
            error = aInstance->mThreadNetif.RegisterCallback(aInstance->mNetifCallback[i]);
            break;
        }
    }

    return error;
}

void otRemoveStateChangeCallback(otInstance *aInstance, otStateChangedCallback aCallback, void *aCallbackContext)
{
    for (size_t i = 0; i < OPENTHREAD_CONFIG_MAX_STATECHANGE_HANDLERS; i++)
    {
        if (aInstance->mNetifCallback[i].IsServing(aCallback, aCallbackContext))
        {
            aInstance->mThreadNetif.RemoveCallback(aInstance->mNetifCallback[i]);
            aInstance->mNetifCallback[i].Free();
            break;
        }
    }
}

const char *otGetVersionString(void)
{
    /**
     * PLATFORM_VERSION_ATTR_PREFIX and PLATFORM_VERSION_ATTR_SUFFIX are
     * intended to be used to specify compiler directives to indicate
     * what linker section the platform version string should be stored.
     *
     * This is useful for specifying an exact locaiton of where the version
     * string will be located so that it can be easily retrieved from the
     * raw firmware image.
     *
     * If PLATFORM_VERSION_ATTR_PREFIX is unspecified, the keyword `static`
     * is used instead.
     *
     * If both are unspecified, the location of the string in the firmware
     * image will be undefined and may change.
     */

#ifdef PLATFORM_VERSION_ATTR_PREFIX
    PLATFORM_VERSION_ATTR_PREFIX
#else
    static
#endif
    const char sVersion[] =
        PACKAGE_NAME "/" PACKAGE_VERSION
#ifdef  PLATFORM_INFO
        "; " PLATFORM_INFO
#endif
#if defined(__DATE__)
        "; " __DATE__ " " __TIME__
#endif
#ifdef PLATFORM_VERSION_ATTR_SUFFIX
        PLATFORM_VERSION_ATTR_SUFFIX
#endif
        ; // Trailing semicolon to end statement.

    return sVersion;
}

ThreadError otSetPreferredRouterId(otInstance *aInstance, uint8_t aRouterId)
{
    return aInstance->mThreadNetif.GetMle().SetPreferredRouterId(aRouterId);
}

void otInstancePostConstructor(otInstance *aInstance)
{
    // restore datasets and network information
    otPlatSettingsInit(aInstance);
    aInstance->mThreadNetif.GetMle().Restore();

#if OPENTHREAD_CONFIG_ENABLE_AUTO_START_SUPPORT

    // If auto start is configured, do that now
    if (otThreadGetAutoStart(aInstance))
    {
        if (otIp6SetEnabled(aInstance, true) == kThreadError_None)
        {
            // Only try to start Thread if we could bring up the interface
            if (otThreadStart(aInstance) != kThreadError_None)
            {
                // Bring the interface down if Thread failed to start
                otIp6SetEnabled(aInstance, false);
            }
        }
    }

#endif
}

#ifdef OPENTHREAD_MULTIPLE_INSTANCE

otInstance *otInstanceInit(void *aInstanceBuffer, size_t *aInstanceBufferSize)
{
    otInstance *aInstance = NULL;

    otLogFuncEntry();
    otLogInfoApi("otInstanceInit");

    VerifyOrExit(aInstanceBufferSize != NULL, ;);

    // Make sure the input buffer is big enough
    VerifyOrExit(sizeof(otInstance) <= *aInstanceBufferSize, *aInstanceBufferSize = sizeof(otInstance));

    VerifyOrExit(aInstanceBuffer != NULL, ;);

    // Construct the context
    aInstance = new(aInstanceBuffer)otInstance();

    // Execute post constructor operations
    otInstancePostConstructor(aInstance);

exit:

    otLogFuncExit();
    return aInstance;
}

#else

otInstance *otInstanceInit()
{
    otLogFuncEntry();

    otLogInfoApi("otInstanceInit");

    VerifyOrExit(sInstance == NULL, ;);

    // Construct the context
    sInstance = new(&sInstanceRaw)otInstance();

    // Execute post constructor operations
    otInstancePostConstructor(sInstance);

exit:

    otLogFuncExit();
    return sInstance;
}

#endif


void otSetReceiveDiagnosticGetCallback(otInstance *aInstance, otReceiveDiagnosticGetCallback aCallback,
                                       void *aCallbackContext)
{
    aInstance->mThreadNetif.GetNetworkDiagnostic().SetReceiveDiagnosticGetCallback(aCallback, aCallbackContext);
}

ThreadError otSendDiagnosticGet(otInstance *aInstance, const otIp6Address *aDestination, const uint8_t aTlvTypes[],
                                uint8_t aCount)
{
    return aInstance->mThreadNetif.GetNetworkDiagnostic().SendDiagnosticGet(*static_cast<const Ip6::Address *>
                                                                            (aDestination),
                                                                            aTlvTypes,
                                                                            aCount);
}

ThreadError otSendDiagnosticReset(otInstance *aInstance, const otIp6Address *aDestination, const uint8_t aTlvTypes[],
                                  uint8_t aCount)
{
    return aInstance->mThreadNetif.GetNetworkDiagnostic().SendDiagnosticReset(*static_cast<const Ip6::Address *>
                                                                              (aDestination),
                                                                              aTlvTypes,
                                                                              aCount);
}

void otInstanceFinalize(otInstance *aInstance)
{
    otLogFuncEntry();

    // Ensure we are disabled
    (void)otThreadStop(aInstance);
    (void)otIp6SetEnabled(aInstance, false);

#ifndef OPENTHREAD_MULTIPLE_INSTANCE
    sInstance = NULL;
#endif
    otLogFuncExit();
}

ThreadError otThreadStart(otInstance *aInstance)
{
    ThreadError error = kThreadError_None;

    otLogFuncEntry();

    VerifyOrExit(aInstance->mThreadNetif.GetMac().GetPanId() != Mac::kPanIdBroadcast, error = kThreadError_InvalidState);

    error = aInstance->mThreadNetif.GetMle().Start(true);

exit:
    otLogFuncExitErr(error);
    return error;
}

ThreadError otThreadStop(otInstance *aInstance)
{
    ThreadError error = kThreadError_None;

    otLogFuncEntry();

    error = aInstance->mThreadNetif.GetMle().Stop(true);

    otLogFuncExitErr(error);
    return error;
}

ThreadError otThreadSetAutoStart(otInstance *aInstance, bool aStartAutomatically)
{
#if OPENTHREAD_CONFIG_ENABLE_AUTO_START_SUPPORT
    uint8_t autoStart = aStartAutomatically ? 1 : 0;
    return otPlatSettingsSet(aInstance, kKeyThreadAutoStart, &autoStart, sizeof(autoStart));
#else
    (void)aInstance;
    (void)aStartAutomatically;
    return kThreadError_NotImplemented;
#endif
}

bool otThreadGetAutoStart(otInstance *aInstance)
{
#if OPENTHREAD_CONFIG_ENABLE_AUTO_START_SUPPORT
    uint8_t autoStart = 0;
    uint16_t autoStartLength = sizeof(autoStart);

    if (otPlatSettingsGet(aInstance, kKeyThreadAutoStart, 0, &autoStart, &autoStartLength) != kThreadError_None)
    {
        autoStart = 0;
    }

    return autoStart != 0;
#else
    (void)aInstance;
    return false;
#endif
}

bool otIsSingleton(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().IsSingleton();
}

ThreadError otDiscover(otInstance *aInstance, uint32_t aScanChannels, uint16_t aScanDuration, uint16_t aPanId,
                       otHandleActiveScanResult aCallback, void *aCallbackContext)
{
    return aInstance->mThreadNetif.GetMle().Discover(aScanChannels, aScanDuration, aPanId, false, aCallback,
                                                     aCallbackContext);
}

bool otIsDiscoverInProgress(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMle().IsDiscoverInProgress();
}

ThreadError otSendMacDataRequest(otInstance *aInstance)
{
    return aInstance->mThreadNetif.GetMeshForwarder().SendMacDataRequest();
}

bool otIcmp6IsEchoEnabled(otInstance *aInstance)
{
    return aInstance->mIp6.mIcmp.IsEchoEnabled();
}

void otIcmp6SetEchoEnabled(otInstance *aInstance, bool aEnabled)
{
    aInstance->mIp6.mIcmp.SetEchoEnabled(aEnabled);
}

ThreadError otIcmp6RegisterHandler(otInstance *aInstance, otIcmp6Handler *aHandler)
{
    return aInstance->mIp6.mIcmp.RegisterHandler(*static_cast<Ip6::IcmpHandler *>(aHandler));
}

ThreadError otIcmp6SendEchoRequest(otInstance *aInstance, otMessage aMessage,
                                   const otMessageInfo *aMessageInfo, uint16_t aIdentifier)
{
    return aInstance->mIp6.mIcmp.SendEchoRequest(*static_cast<Message *>(aMessage),
                                                 *static_cast<const Ip6::MessageInfo *>(aMessageInfo),
                                                 aIdentifier);
}

ThreadError otGetActiveDataset(otInstance *aInstance, otOperationalDataset *aDataset)
{
    ThreadError error = kThreadError_None;

    VerifyOrExit(aDataset != NULL, error = kThreadError_InvalidArgs);

    aInstance->mThreadNetif.GetActiveDataset().GetLocal().Get(*aDataset);

exit:
    return error;
}

ThreadError otSetActiveDataset(otInstance *aInstance, const otOperationalDataset *aDataset)
{
    ThreadError error;

    VerifyOrExit(aDataset != NULL, error = kThreadError_InvalidArgs);

    error = aInstance->mThreadNetif.GetActiveDataset().Set(*aDataset);

exit:
    return error;
}

bool otIsNodeCommissioned(otInstance *aInstance)
{
    otOperationalDataset dataset;

    otGetActiveDataset(aInstance, &dataset);

    if ((dataset.mIsMasterKeySet) && (dataset.mIsNetworkNameSet) &&
        (dataset.mIsExtendedPanIdSet) && (dataset.mIsPanIdSet) && (dataset.mIsChannelSet))
    {
        return true;
    }

    return false;
}

ThreadError otGetPendingDataset(otInstance *aInstance, otOperationalDataset *aDataset)
{
    ThreadError error = kThreadError_None;

    VerifyOrExit(aDataset != NULL, error = kThreadError_InvalidArgs);

    aInstance->mThreadNetif.GetPendingDataset().GetLocal().Get(*aDataset);

exit:
    return error;
}

ThreadError otSetPendingDataset(otInstance *aInstance, const otOperationalDataset *aDataset)
{
    ThreadError error;

    VerifyOrExit(aDataset != NULL, error = kThreadError_InvalidArgs);

    error = aInstance->mThreadNetif.GetPendingDataset().Set(*aDataset);

exit:
    return error;
}

ThreadError otSendActiveGet(otInstance *aInstance, const uint8_t *aTlvTypes, uint8_t aLength,
                            const otIp6Address *aAddress)
{
    return aInstance->mThreadNetif.GetActiveDataset().SendGetRequest(aTlvTypes, aLength, aAddress);
}

ThreadError otSendActiveSet(otInstance *aInstance, const otOperationalDataset *aDataset, const uint8_t *aTlvs,
                            uint8_t aLength)
{
    return aInstance->mThreadNetif.GetActiveDataset().SendSetRequest(*aDataset, aTlvs, aLength);
}

ThreadError otSendPendingGet(otInstance *aInstance, const uint8_t *aTlvTypes, uint8_t aLength,
                             const otIp6Address *aAddress)
{
    return aInstance->mThreadNetif.GetPendingDataset().SendGetRequest(aTlvTypes, aLength, aAddress);
}

ThreadError otSendPendingSet(otInstance *aInstance, const otOperationalDataset *aDataset, const uint8_t *aTlvs,
                             uint8_t aLength)
{
    return aInstance->mThreadNetif.GetPendingDataset().SendSetRequest(*aDataset, aTlvs, aLength);
}

#ifdef __cplusplus
}  // extern "C"
#endif

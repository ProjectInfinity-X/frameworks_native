/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef COMMANDS_H_
#define COMMANDS_H_

#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>

#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <android-base/macros.h>
#include <binder/BinderService.h>
#include <cutils/multiuser.h>

#include "android/os/BnInstalld.h"
#include "installd_constants.h"

namespace android {
namespace installd {

using IFsveritySetupAuthToken = android::os::IInstalld::IFsveritySetupAuthToken;

class InstalldNativeService : public BinderService<InstalldNativeService>, public os::BnInstalld {
public:
    class FsveritySetupAuthToken : public os::IInstalld::BnFsveritySetupAuthToken {
    public:
        FsveritySetupAuthToken() : mStatFromAuthFd() {}

        binder::Status authenticate(const android::os::ParcelFileDescriptor& authFd, int32_t uid);
        bool isSameStat(const struct stat& st) const;

    private:
        // Not copyable or movable
        FsveritySetupAuthToken(const FsveritySetupAuthToken&) = delete;
        FsveritySetupAuthToken& operator=(const FsveritySetupAuthToken&) = delete;

        struct stat mStatFromAuthFd;
    };

    static status_t start();
    static char const* getServiceName() { return "installd"; }
    virtual status_t dump(int fd, const Vector<String16> &args) override;

    binder::Status createUserData(const std::optional<std::string>& uuid, int32_t userId,
            int32_t userSerial, int32_t flags);
    binder::Status destroyUserData(const std::optional<std::string>& uuid, int32_t userId,
            int32_t flags);

    binder::Status createAppData(const std::optional<std::string>& uuid,
                                 const std::string& packageName, int32_t userId, int32_t flags,
                                 int32_t appId, int32_t previousAppId, const std::string& seInfo,
                                 int32_t targetSdkVersion, int64_t* ceDataInode,
                                 int64_t* deDataInode);

    binder::Status createAppData(
            const android::os::CreateAppDataArgs& args,
            android::os::CreateAppDataResult* _aidl_return);
    binder::Status createAppDataBatched(
            const std::vector<android::os::CreateAppDataArgs>& args,
            std::vector<android::os::CreateAppDataResult>* _aidl_return);

    binder::Status reconcileSdkData(const android::os::ReconcileSdkDataArgs& args);

    binder::Status restoreconAppData(const std::optional<std::string>& uuid,
            const std::string& packageName, int32_t userId, int32_t flags, int32_t appId,
            const std::string& seInfo);

    binder::Status migrateAppData(const std::optional<std::string>& uuid,
            const std::string& packageName, int32_t userId, int32_t flags);
    binder::Status clearAppData(const std::optional<std::string>& uuid,
            const std::string& packageName, int32_t userId, int32_t flags, int64_t ceDataInode);
    binder::Status destroyAppData(const std::optional<std::string>& uuid,
            const std::string& packageName, int32_t userId, int32_t flags, int64_t ceDataInode);

    binder::Status fixupAppData(const std::optional<std::string>& uuid, int32_t flags);

    binder::Status snapshotAppData(const std::optional<std::string>& volumeUuid,
            const std::string& packageName, const int32_t user, const int32_t snapshotId,
            int32_t storageFlags, int64_t* _aidl_return);
    binder::Status restoreAppDataSnapshot(const std::optional<std::string>& volumeUuid,
            const std::string& packageName, const int32_t appId, const std::string& seInfo,
            const int32_t user, const int32_t snapshotId, int32_t storageFlags);
    binder::Status destroyAppDataSnapshot(const std::optional<std::string> &volumeUuid,
            const std::string& packageName, const int32_t user, const int64_t ceSnapshotInode,
            const int32_t snapshotId, int32_t storageFlags);
    binder::Status destroyCeSnapshotsNotSpecified(const std::optional<std::string> &volumeUuid,
            const int32_t user, const std::vector<int32_t>& retainSnapshotIds);

    binder::Status getAppSize(const std::optional<std::string>& uuid,
            const std::vector<std::string>& packageNames, int32_t userId, int32_t flags,
            int32_t appId, const std::vector<int64_t>& ceDataInodes,
            const std::vector<std::string>& codePaths, std::vector<int64_t>* _aidl_return);
    binder::Status getUserSize(const std::optional<std::string>& uuid,
            int32_t userId, int32_t flags, const std::vector<int32_t>& appIds,
            std::vector<int64_t>* _aidl_return);
    binder::Status getExternalSize(const std::optional<std::string>& uuid,
            int32_t userId, int32_t flags, const std::vector<int32_t>& appIds,
            std::vector<int64_t>* _aidl_return);

    binder::Status getAppCrates(const std::optional<std::string>& uuid,
            const std::vector<std::string>& packageNames,
            int32_t userId,
            std::optional<std::vector<std::optional<android::os::storage::CrateMetadata>>>*
                    _aidl_return);
    binder::Status getUserCrates(
            const std::optional<std::string>& uuid, int32_t userId,
            std::optional<std::vector<std::optional<android::os::storage::CrateMetadata>>>*
                    _aidl_return);

    binder::Status setAppQuota(const std::optional<std::string>& uuid,
            int32_t userId, int32_t appId, int64_t cacheQuota);

    binder::Status moveCompleteApp(const std::optional<std::string>& fromUuid,
            const std::optional<std::string>& toUuid, const std::string& packageName,
            int32_t appId, const std::string& seInfo,
            int32_t targetSdkVersion, const std::string& fromCodePath);

    binder::Status dexopt(const std::string& apkPath, int32_t uid, const std::string& packageName,
                          const std::string& instructionSet, int32_t dexoptNeeded,
                          const std::optional<std::string>& outputPath, int32_t dexFlags,
                          const std::string& compilerFilter, const std::optional<std::string>& uuid,
                          const std::optional<std::string>& classLoaderContext,
                          const std::optional<std::string>& seInfo, bool downgrade,
                          int32_t targetSdkVersion, const std::optional<std::string>& profileName,
                          const std::optional<std::string>& dexMetadataPath,
                          const std::optional<std::string>& compilationReason, bool* aidl_return);

    binder::Status controlDexOptBlocking(bool block);

    binder::Status rmdex(const std::string& codePath, const std::string& instructionSet);

    binder::Status mergeProfiles(int32_t uid, const std::string& packageName,
            const std::string& profileName, int* _aidl_return);
    binder::Status dumpProfiles(int32_t uid, const std::string& packageName,
                                const std::string& profileName, const std::string& codePath,
                                bool dumpClassesAndMethods, bool* _aidl_return);
    binder::Status copySystemProfile(const std::string& systemProfile,
            int32_t uid, const std::string& packageName, const std::string& profileName,
            bool* _aidl_return);
    binder::Status clearAppProfiles(const std::string& packageName, const std::string& profileName);
    binder::Status destroyAppProfiles(const std::string& packageName);
    binder::Status deleteReferenceProfile(const std::string& packageName,
                                          const std::string& profileName);

    binder::Status createProfileSnapshot(int32_t appId, const std::string& packageName,
            const std::string& profileName, const std::string& classpath, bool* _aidl_return);
    binder::Status destroyProfileSnapshot(const std::string& packageName,
            const std::string& profileName);

    binder::Status rmPackageDir(const std::string& packageName, const std::string& packageDir);
    binder::Status freeCache(const std::optional<std::string>& uuid, int64_t targetFreeBytes,
            int32_t flags);
    binder::Status linkNativeLibraryDirectory(const std::optional<std::string>& uuid,
            const std::string& packageName, const std::string& nativeLibPath32, int32_t userId);
    binder::Status createOatDir(const std::string& packageName, const std::string& oatDir,
                                const std::string& instructionSet);
    binder::Status linkFile(const std::string& packageName, const std::string& relativePath,
                            const std::string& fromBase, const std::string& toBase);
    binder::Status moveAb(const std::string& packageName, const std::string& apkPath,
                          const std::string& instructionSet, const std::string& outputPath);
    binder::Status deleteOdex(const std::string& packageName, const std::string& apkPath,
                              const std::string& instructionSet,
                              const std::optional<std::string>& outputPath, int64_t* _aidl_return);
    binder::Status reconcileSecondaryDexFile(const std::string& dexPath,
        const std::string& packageName, int32_t uid, const std::vector<std::string>& isa,
        const std::optional<std::string>& volumeUuid, int32_t storage_flag, bool* _aidl_return);
    binder::Status hashSecondaryDexFile(const std::string& dexPath,
        const std::string& packageName, int32_t uid, const std::optional<std::string>& volumeUuid,
        int32_t storageFlag, std::vector<uint8_t>* _aidl_return);

    binder::Status invalidateMounts();
    binder::Status setFirstBoot();
    binder::Status isQuotaSupported(const std::optional<std::string>& volumeUuid,
            bool* _aidl_return);
    binder::Status tryMountDataMirror(const std::optional<std::string>& volumeUuid);
    binder::Status onPrivateVolumeRemoved(const std::optional<std::string>& volumeUuid);

    binder::Status prepareAppProfile(const std::string& packageName,
            int32_t userId, int32_t appId, const std::string& profileName,
            const std::string& codePath, const std::optional<std::string>& dexMetadata,
            bool* _aidl_return);

    binder::Status migrateLegacyObbData();

    binder::Status cleanupInvalidPackageDirs(const std::optional<std::string>& uuid, int32_t userId,
                                             int32_t flags);

    binder::Status getOdexVisibility(const std::string& packageName, const std::string& apkPath,
                                     const std::string& instructionSet,
                                     const std::optional<std::string>& outputPath,
                                     int32_t* _aidl_return);

    binder::Status createFsveritySetupAuthToken(const android::os::ParcelFileDescriptor& authFd,
                                                int32_t uid,
                                                android::sp<IFsveritySetupAuthToken>* _aidl_return);
    binder::Status enableFsverity(const android::sp<IFsveritySetupAuthToken>& authToken,
                                  const std::string& filePath, const std::string& packageName,
                                  int32_t* _aidl_return);

private:
    std::recursive_mutex mLock;
    std::unordered_map<userid_t, std::weak_ptr<std::shared_mutex>> mUserIdLock;
    std::unordered_map<std::string, std::weak_ptr<std::recursive_mutex>> mPackageNameLock;

    std::recursive_mutex mMountsLock;
    std::recursive_mutex mQuotasLock;

    /* Map of all storage mounts from source to target */
    std::unordered_map<std::string, std::string> mStorageMounts;

    /* Map from UID to cache quota size */
    std::unordered_map<uid_t, int64_t> mCacheQuotas;

    std::string findDataMediaPath(const std::optional<std::string>& uuid, userid_t userid);

    binder::Status createAppDataLocked(const std::optional<std::string>& uuid,
                                       const std::string& packageName, int32_t userId,
                                       int32_t flags, int32_t appId, int32_t previousAppId,
                                       const std::string& seInfo, int32_t targetSdkVersion,
                                       int64_t* ceDataInode, int64_t* deDataInode);
    binder::Status restoreconAppDataLocked(const std::optional<std::string>& uuid,
                                           const std::string& packageName, int32_t userId,
                                           int32_t flags, int32_t appId, const std::string& seInfo);

    binder::Status createSdkSandboxDataPackageDirectory(const std::optional<std::string>& uuid,
                                                        const std::string& packageName,
                                                        int32_t userId, int32_t appId,
                                                        int32_t flags);
    binder::Status clearSdkSandboxDataPackageDirectory(const std::optional<std::string>& uuid,
                                                       const std::string& packageName,
                                                       int32_t userId, int32_t flags);
    binder::Status destroySdkSandboxDataPackageDirectory(const std::optional<std::string>& uuid,
                                                         const std::string& packageName,
                                                         int32_t userId, int32_t flags);
    binder::Status reconcileSdkData(const std::optional<std::string>& uuid,
                                    const std::string& packageName,
                                    const std::vector<std::string>& subDirNames, int32_t userId,
                                    int32_t appId, int32_t previousAppId, const std::string& seInfo,
                                    int flags);
    binder::Status restoreconSdkDataLocked(const std::optional<std::string>& uuid,
                                           const std::string& packageName, int32_t userId,
                                           int32_t flags, int32_t appId, const std::string& seInfo);
};

}  // namespace installd
}  // namespace android

#endif  // COMMANDS_H_

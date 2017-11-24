/****************************************************************************
 Copyright (c) 2014 cocos2d-x.org

 http://www.cocos2d-x.org

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/
#include "AssetsManagerEx.h"
#include "CCEventListenerAssetsManagerEx.h"
#include "base/ccUTF8.h"
#include "base/CCDirector.h"

#include <stdio.h>

#ifdef MINIZIP_FROM_SYSTEM
#include <minizip/unzip.h>
#else // from our embedded sources
#include "unzip.h"
#endif
#include "base/CCAsyncTaskPool.h"

NS_CC_EXT_BEGIN

#define TEMP_PACKAGE_SUFFIX     "_temp"
#define VERSION_FILENAME        "version.manifest"
#define TEMP_MANIFEST_FILENAME  "project.manifest.temp"
#define MANIFEST_FILENAME       "project.manifest"

#define BUFFER_SIZE    8192
#define MAX_FILENAME   512

#define DEFAULT_CONNECTION_TIMEOUT 45

#define SAVE_POINT_INTERVAL 0.1

const std::string AssetsManagerEx::VERSION_ID = "@version";
const std::string AssetsManagerEx::MANIFEST_ID = "@manifest";

// Implementation of AssetsManagerEx

AssetsManagerEx::AssetsManagerEx(const std::string& manifestUrl, const std::string& storagePath)
: _updateState(State::UNCHECKED)
, _assets(nullptr)
, _storagePath("")
, _tempVersionPath("")
, _cacheManifestPath("")
, _tempManifestPath("")
, _manifestUrl(manifestUrl)
, _localManifest(nullptr)
, _tempManifest(nullptr)
, _remoteManifest(nullptr)
, _updateEntry(UpdateEntry::NONE)
, _percent(0)
, _percentByFile(0)
, _totalToDownload(0)
, _totalWaitToDownload(0)
, _nextSavePoint(0.0)
, _maxConcurrentTask(32)
, _currConcurrentTask(0)
, _versionCompareHandle(nullptr)
, _verifyCallback(nullptr)
, _inited(false)
{
    // Init variables
    _eventDispatcher = Director::getInstance()->getEventDispatcher();
    std::string pointer = StringUtils::format("%p", this);
    _eventName = EventListenerAssetsManagerEx::LISTENER_ID + pointer;
    _fileUtils = FileUtils::getInstance();

    network::DownloaderHints hints =
    {
        static_cast<uint32_t>(_maxConcurrentTask),
        DEFAULT_CONNECTION_TIMEOUT,
        ".tmp"
    };
    _downloader = std::shared_ptr<network::Downloader>(new network::Downloader(hints));
    _downloader->onTaskError = std::bind(&AssetsManagerEx::onError, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    _downloader->onTaskProgress = [this](const network::DownloadTask& task,
                                         int64_t /*bytesReceived*/,
                                         int64_t totalBytesReceived,
                                         int64_t totalBytesExpected)
    {
        this->onProgress(totalBytesExpected, totalBytesReceived, task.requestURL, task.identifier);
    };
    _downloader->onFileTaskSuccess = [this](const network::DownloadTask& task)
    {
        this->onSuccess(task.requestURL, task.storagePath, task.identifier);
    };
    setStoragePath(storagePath);
    _tempVersionPath = _tempStoragePath + VERSION_FILENAME; //下载下来的 临时version.manifest
    _cacheManifestPath = _storagePath + MANIFEST_FILENAME; //下载下来的 project.manifest 保留的是上次的manifest
    _tempManifestPath = _tempStoragePath + TEMP_MANIFEST_FILENAME; //下载下来的历史 project.manifest

    initManifests(manifestUrl);
}

AssetsManagerEx::~AssetsManagerEx() //析构函数
{
	//释放下载器
    _downloader->onTaskError = (nullptr);
    _downloader->onFileTaskSuccess = (nullptr);
    _downloader->onTaskProgress = (nullptr);

	//释放本地的Manifest
    CC_SAFE_RELEASE(_localManifest);
    // _tempManifest could share a ptr with _remoteManifest or _localManifest
    if (_tempManifest != _localManifest && _tempManifest != _remoteManifest)
        CC_SAFE_RELEASE(_tempManifest); //释放下载下来的 临时的Manifest
    CC_SAFE_RELEASE(_remoteManifest); //释放下载下来的Manefest 实际上这个manifest就是用下载下来的临时的manifest来初始化的
}

//manifestUrl 本地的manifest的路径
//storagPath 下载下来存放manifest 的路径
AssetsManagerEx* AssetsManagerEx::create(const std::string& manifestUrl, const std::string& storagePath)
{
    AssetsManagerEx* ret = new (std::nothrow) AssetsManagerEx(manifestUrl, storagePath);
    if (ret)
    {
        ret->autorelease();
    }
    else
    {
        CC_SAFE_DELETE(ret);
    }
    return ret;
}

//用文件初始化manifest类 初始化的是本地的 manifest
void AssetsManagerEx::initManifests(const std::string& manifestUrl)
{
    _inited = true;
    // Init and load local manifest
    _localManifest = new (std::nothrow) Manifest();
    if (_localManifest)
    {
        loadLocalManifest(manifestUrl); //初始化本地的manifest
        
        // Init and load temporary manifest
        _tempManifest = new (std::nothrow) Manifest();
        if (_tempManifest)
        {
            _tempManifest->parse(_tempManifestPath); //用下载来的 临时的manife出初始化tempManifest 解析manifest
            // Previous update is interrupted
            if (_fileUtils->isFileExist(_tempManifestPath))
            {
                // Manifest parse failed, remove all temp files
                if (!_tempManifest->isLoaded())
                {
                    _fileUtils->removeDirectory(_tempStoragePath);
                    CC_SAFE_RELEASE(_tempManifest);
                    _tempManifest = nullptr;
                }
            }
        }
        else
        {
            _inited = false;
        }
        
        // Init remote manifest for future usage
        _remoteManifest = new (std::nothrow) Manifest();
        if (!_remoteManifest)
        {
            _inited = false;
        }
    }
    else
    {
        _inited = false;
    }
    
    if (!_inited)//初始化失败
    {
        CC_SAFE_RELEASE(_localManifest);
        CC_SAFE_RELEASE(_tempManifest);
        CC_SAFE_RELEASE(_remoteManifest);
        _localManifest = nullptr;
        _tempManifest = nullptr;
        _remoteManifest = nullptr;
    }
}

void AssetsManagerEx::prepareLocalManifest()
{
    // An alias to assets
    _assets = &(_localManifest->getAssets());//下载的资源

    // Add search paths
    _localManifest->prependSearchPaths();
}

void AssetsManagerEx::loadLocalManifest(const std::string& /*manifestUrl*/)
{
    Manifest *cachedManifest = nullptr;
    // Find the cached manifest file
    if (_fileUtils->isFileExist(_cacheManifestPath)) //如果下载目录里面有manifest就去解析 下载目录里面的manifest
    {
        cachedManifest = new (std::nothrow) Manifest();
        if (cachedManifest) {
            cachedManifest->parse(_cacheManifestPath);
            if (!cachedManifest->isLoaded())
            {
                _fileUtils->removeFile(_cacheManifestPath);
                CC_SAFE_RELEASE(cachedManifest);
                cachedManifest = nullptr;
            }
        }
    }
    
    // Ensure no search path of cached manifest is used to load this manifest
    std::vector<std::string> searchPaths = _fileUtils->getSearchPaths();
    if (cachedManifest)
    {
        std::vector<std::string> cacheSearchPaths = cachedManifest->getSearchPaths();
        std::vector<std::string> trimmedPaths = searchPaths;
        for (auto path : cacheSearchPaths)
        {
            const auto pos = std::find(trimmedPaths.begin(), trimmedPaths.end(), path);
            if (pos != trimmedPaths.end())
            {
                trimmedPaths.erase(pos);
            }
        }
        _fileUtils->setSearchPaths(trimmedPaths);
    }
    // Load local manifest in app package
    _localManifest->parse(_manifestUrl);
    if (cachedManifest) {
        // Restore search paths
        _fileUtils->setSearchPaths(searchPaths);
    }
    if (_localManifest->isLoaded())
    {
        // Compare with cached manifest to determine which one to use
        if (cachedManifest) { //这个cachedManifest 是用的上次下载的
            bool localNewer = _localManifest->versionGreater(cachedManifest, _versionCompareHandle);
            if (localNewer) //本地的 的版本 更高
            {
                // Recreate storage, to empty the content
                _fileUtils->removeDirectory(_storagePath);
                _fileUtils->createDirectory(_storagePath);
                CC_SAFE_RELEASE(cachedManifest);
            }
            else //本地的版本更低
            {
                CC_SAFE_RELEASE(_localManifest);
                _localManifest = cachedManifest;
            }
        }
        prepareLocalManifest();
    }

    // Fail to load local manifest
    if (!_localManifest->isLoaded())
    {
        CCLOG("AssetsManagerEx : No local manifest file found error.\n");
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_NO_LOCAL_MANIFEST);
    }
}

std::string AssetsManagerEx::basename(const std::string& path) const
{
    size_t found = path.find_last_of("/\\");
    
    if (std::string::npos != found)
    {
        return path.substr(0, found);
    }
    else
    {
        return path;
    }
}

std::string AssetsManagerEx::get(const std::string& key) const
{
    auto it = _assets->find(key);
    if (it != _assets->cend()) {
        return _storagePath + it->second.path;
    }
    else return "";
}

const Manifest* AssetsManagerEx::getLocalManifest() const
{
    return _localManifest;
}

const Manifest* AssetsManagerEx::getRemoteManifest() const
{
    return _remoteManifest;
}

const std::string& AssetsManagerEx::getStoragePath() const
{
    return _storagePath;
}

void AssetsManagerEx::setStoragePath(const std::string& storagePath)
{
    _storagePath = storagePath;
    adjustPath(_storagePath);
    _fileUtils->createDirectory(_storagePath);
    
    _tempStoragePath = _storagePath;
    _tempStoragePath.insert(_storagePath.size() - 1, TEMP_PACKAGE_SUFFIX);
    _fileUtils->createDirectory(_tempStoragePath);
}

void AssetsManagerEx::adjustPath(std::string &path)
{
    if (path.size() > 0 && path[path.size() - 1] != '/')
    {
        path.append("/");
    }
}

bool AssetsManagerEx::decompress(const std::string &zip)
{
    // Find root path for zip file
    size_t pos = zip.find_last_of("/\\");
    if (pos == std::string::npos)
    {
        CCLOG("AssetsManagerEx : no root path specified for zip file %s\n", zip.c_str());
        return false;
    }
    const std::string rootPath = zip.substr(0, pos+1);
    
    // Open the zip file
    unzFile zipfile = unzOpen(FileUtils::getInstance()->getSuitableFOpen(zip).c_str());
    if (! zipfile)
    {
        CCLOG("AssetsManagerEx : can not open downloaded zip file %s\n", zip.c_str());
        return false;
    }
    
    // Get info about the zip file
    unz_global_info global_info;
    if (unzGetGlobalInfo(zipfile, &global_info) != UNZ_OK)
    {
        CCLOG("AssetsManagerEx : can not read file global info of %s\n", zip.c_str());
        unzClose(zipfile);
        return false;
    }
    
    // Buffer to hold data read from the zip file
    char readBuffer[BUFFER_SIZE];
    // Loop to extract all files.
    uLong i;
    for (i = 0; i < global_info.number_entry; ++i)
    {
        // Get info about current file.
        unz_file_info fileInfo;
        char fileName[MAX_FILENAME];
        if (unzGetCurrentFileInfo(zipfile,
                                  &fileInfo,
                                  fileName,
                                  MAX_FILENAME,
                                  NULL,
                                  0,
                                  NULL,
                                  0) != UNZ_OK)
        {
            CCLOG("AssetsManagerEx : can not read compressed file info\n");
            unzClose(zipfile);
            return false;
        }
        const std::string fullPath = rootPath + fileName;
        
        // Check if this entry is a directory or a file.
        const size_t filenameLength = strlen(fileName);
        if (fileName[filenameLength-1] == '/')
        {
            //There are not directory entry in some case.
            //So we need to create directory when decompressing file entry
            if ( !_fileUtils->createDirectory(basename(fullPath)) )
            {
                // Failed to create directory
                CCLOG("AssetsManagerEx : can not create directory %s\n", fullPath.c_str());
                unzClose(zipfile);
                return false;
            }
        }
        else
        {
            // Create all directories in advance to avoid issue
            std::string dir = basename(fullPath);
            if (!_fileUtils->isDirectoryExist(dir)) {
                if (!_fileUtils->createDirectory(dir)) {
                    // Failed to create directory
                    CCLOG("AssetsManagerEx : can not create directory %s\n", fullPath.c_str());
                    unzClose(zipfile);
                    return false;
                }
            }
            // Entry is a file, so extract it.
            // Open current file.
            if (unzOpenCurrentFile(zipfile) != UNZ_OK)
            {
                CCLOG("AssetsManagerEx : can not extract file %s\n", fileName);
                unzClose(zipfile);
                return false;
            }
            
            // Create a file to store current file.
            FILE *out = fopen(FileUtils::getInstance()->getSuitableFOpen(fullPath).c_str(), "wb");
            if (!out)
            {
                CCLOG("AssetsManagerEx : can not create decompress destination file %s (errno: %d)\n", fullPath.c_str(), errno);
                unzCloseCurrentFile(zipfile);
                unzClose(zipfile);
                return false;
            }
            
            // Write current file content to destinate file.
            int error = UNZ_OK;
            do
            {
                error = unzReadCurrentFile(zipfile, readBuffer, BUFFER_SIZE);
                if (error < 0)
                {
                    CCLOG("AssetsManagerEx : can not read zip file %s, error code is %d\n", fileName, error);
                    fclose(out);
                    unzCloseCurrentFile(zipfile);
                    unzClose(zipfile);
                    return false;
                }
                
                if (error > 0)
                {
                    fwrite(readBuffer, error, 1, out);
                }
            } while(error > 0);
            
            fclose(out);
        }
        
        unzCloseCurrentFile(zipfile);
        
        // Goto next entry listed in the zip file.
        if ((i+1) < global_info.number_entry)
        {
            if (unzGoToNextFile(zipfile) != UNZ_OK)
            {
                CCLOG("AssetsManagerEx : can not read next file for decompressing\n");
                unzClose(zipfile);
                return false;
            }
        }
    }
    
    unzClose(zipfile);
    return true;
}

void AssetsManagerEx::decompressDownloadedZip(const std::string &customId, const std::string &storagePath)//解压zip
{
    struct AsyncData
    {
        std::string customId;
        std::string zipFile;
        bool succeed;
    };
    
    AsyncData* asyncData = new AsyncData;
    asyncData->customId = customId;
    asyncData->zipFile = storagePath;
    asyncData->succeed = false;
    
    std::function<void(void*)> decompressFinished = [this](void* param) {
        auto dataInner = reinterpret_cast<AsyncData*>(param);
        if (dataInner->succeed)
        {
            fileSuccess(dataInner->customId, dataInner->zipFile);
        }
        else
        {
            std::string errorMsg = "Unable to decompress file " + dataInner->zipFile;
            // Ensure zip file deletion (if decompress failure cause task thread exit anormally)
            _fileUtils->removeFile(dataInner->zipFile);
            dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_DECOMPRESS, "", errorMsg);
            fileError(dataInner->customId, errorMsg);
        }
        delete dataInner;
    };
    AsyncTaskPool::getInstance()->enqueue(AsyncTaskPool::TaskType::TASK_OTHER, std::move(decompressFinished), (void*)asyncData, [this, asyncData]() {
        // Decompress all compressed files
        if (decompress(asyncData->zipFile))
        {
            asyncData->succeed = true;
        }
        _fileUtils->removeFile(asyncData->zipFile);
    });
}

//时间分发
void AssetsManagerEx::dispatchUpdateEvent(EventAssetsManagerEx::EventCode code, const std::string &assetId/* = ""*/, const std::string &message/* = ""*/, int curle_code/* = CURLE_OK*/, int curlm_code/* = CURLM_OK*/)
{
    switch (code)
    {
        case EventAssetsManagerEx::EventCode::ERROR_UPDATING:
        case EventAssetsManagerEx::EventCode::ERROR_PARSE_MANIFEST:
        case EventAssetsManagerEx::EventCode::ERROR_NO_LOCAL_MANIFEST:
        case EventAssetsManagerEx::EventCode::ERROR_DECOMPRESS:
        case EventAssetsManagerEx::EventCode::ERROR_DOWNLOAD_MANIFEST:
        case EventAssetsManagerEx::EventCode::UPDATE_FAILED:
        case EventAssetsManagerEx::EventCode::UPDATE_FINISHED:
        case EventAssetsManagerEx::EventCode::ALREADY_UP_TO_DATE:
            _updateEntry = UpdateEntry::NONE;
            break;
        case EventAssetsManagerEx::EventCode::UPDATE_PROGRESSION:
            break;
        case EventAssetsManagerEx::EventCode::ASSET_UPDATED:
            break;
        case EventAssetsManagerEx::EventCode::NEW_VERSION_FOUND:
            if (_updateEntry == UpdateEntry::CHECK_UPDATE)
            {
                _updateEntry = UpdateEntry::NONE;
            }
            break;
        default:
            break;
    }

    EventAssetsManagerEx event(_eventName, this, code, _percent, _percentByFile, assetId, message, curle_code, curlm_code);
    _eventDispatcher->dispatchEvent(&event);
}

AssetsManagerEx::State AssetsManagerEx::getState() const
{
    return _updateState;
}

void AssetsManagerEx::downloadVersion()//下载version文件 只有远程才会存放version文件本地是没有version文件的
{
    if (_updateState > State::PREDOWNLOAD_VERSION)
        return;

    std::string versionUrl = _localManifest->getVersionFileUrl();//通过本地的 manifest获得远程version的下载地址

    if (versionUrl.size() > 0) //如果version地址存在
    {
        _updateState = State::DOWNLOADING_VERSION;
        // Download version file asynchronously
        _downloader->createDownloadFileTask(versionUrl, _tempVersionPath, VERSION_ID); //创建version的下载任务
    }
    // No version file found
    else
    {
        CCLOG("AssetsManagerEx : No version file found, step skipped\n");
        _updateState = State::PREDOWNLOAD_MANIFEST;
        downloadManifest(); //无法获取manifest的下载信息就去下载 Manifest 加载manifest
    }
}

void AssetsManagerEx::parseVersion() //解析版本
{
    if (_updateState != State::VERSION_LOADED)
        return;

    _remoteManifest->parseVersion(_tempVersionPath); //-----------------------

    if (!_remoteManifest->isVersionLoaded()) //如果没有加载Manifest 的版本信息
    {
        CCLOG("AssetsManagerEx : Fail to parse version file, step skipped\n");
        _updateState = State::PREDOWNLOAD_MANIFEST;
        downloadManifest(); //下载并且加载Manifest
    }
    else
    {
        if (_localManifest->versionGreater(_remoteManifest, _versionCompareHandle)) //本地版本更高
        {
            _updateState = State::UP_TO_DATE;
            _fileUtils->removeDirectory(_tempStoragePath);
            dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ALREADY_UP_TO_DATE);
        }
        else //本地版本低
        {
            _updateState = State::NEED_UPDATE;

            // Wait to update so continue the process
            if (_updateEntry == UpdateEntry::DO_UPDATE) //需要下载
            {
                // dispatch after checking update entry because event dispatching may modify the update entry
                dispatchUpdateEvent(EventAssetsManagerEx::EventCode::NEW_VERSION_FOUND);
                _updateState = State::PREDOWNLOAD_MANIFEST;
                downloadManifest(); 
            }
            else
            {
                dispatchUpdateEvent(EventAssetsManagerEx::EventCode::NEW_VERSION_FOUND);
            }
        }
    }
}

void AssetsManagerEx::downloadManifest() //下载manifest
{
    if (_updateState != State::PREDOWNLOAD_MANIFEST)
        return;

    std::string manifestUrl;
    if (_remoteManifest->isVersionLoaded()) { //远程的manifest是否已经加载了version信息
        manifestUrl = _remoteManifest->getManifestFileUrl(); 
    } else {
        manifestUrl = _localManifest->getManifestFileUrl();//获取本地的Manifest配置的 远程Manifest的下载地址
    }

    if (manifestUrl.size() > 0)
    {
        _updateState = State::DOWNLOADING_MANIFEST;
        // Download version file asynchronously
        _downloader->createDownloadFileTask(manifestUrl, _tempManifestPath, MANIFEST_ID); //下载远程的manifest
    }
    // No manifest file found
    else
    {
        CCLOG("AssetsManagerEx : No manifest file found, check update failed\n");
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_DOWNLOAD_MANIFEST);
        _updateState = State::UNCHECKED;
    }
}

void AssetsManagerEx::parseManifest()//解析Manifest
{
    if (_updateState != State::MANIFEST_LOADED)
        return;

    _remoteManifest->parse(_tempManifestPath);//装载下载下来的manifest

    if (!_remoteManifest->isLoaded())
    {
        CCLOG("AssetsManagerEx : Error parsing manifest file, %s", _tempManifestPath.c_str());
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_PARSE_MANIFEST);
        _updateState = State::UNCHECKED;//如果没有加载成功 就再从UNCHECK走一遍
    }
    else
    {
        if (_localManifest->versionGreater(_remoteManifest, _versionCompareHandle)) //当前版本比服务器的版本高
        {
            _updateState = State::UP_TO_DATE;
            _fileUtils->removeDirectory(_tempStoragePath);
            dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ALREADY_UP_TO_DATE);
        }
        else
        {
            _updateState = State::NEED_UPDATE;//当前版本比服务器的版本低就开始更新
            dispatchUpdateEvent(EventAssetsManagerEx::EventCode::NEW_VERSION_FOUND);

            if (_updateEntry == UpdateEntry::DO_UPDATE)
            {
                startUpdate();
            }
        }
    }
}

void AssetsManagerEx::startUpdate()
{
    if (_updateState != State::NEED_UPDATE)
        return;

    _updateState = State::UPDATING;
    // Clean up before update
	//清理以及初始化一些本地的缓存
    _failedUnits.clear();
    _downloadUnits.clear();
    _totalWaitToDownload = _totalToDownload = 0;
    _nextSavePoint = 0;
    _percent = _percentByFile = _sizeCollected = _totalSize = 0;
    _downloadedSize.clear();
    _totalEnabled = false;
    
    // Temporary manifest exists, resuming previous download
    if (_tempManifest && _tempManifest->isLoaded() && _tempManifest->versionEquals(_remoteManifest)) //如果有groupVersion 并且GroupVersion 完全匹配
    {
        _tempManifest->saveToFile(_tempManifestPath); //保存临时的manifest
        _tempManifest->genResumeAssetsList(&_downloadUnits);//保存要下载的 asset manifest里面的全部asset
        _totalWaitToDownload = _totalToDownload = (int)_downloadUnits.size(); //要下载的文件数量
        this->batchDownload();
        
        std::string msg = StringUtils::format("Resuming from previous unfinished update, %d files remains to be finished.", _totalToDownload);
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::UPDATE_PROGRESSION, "", msg);
    }
    else
    {
        // Temporary manifest exists, but can't be parsed or version doesn't equals remote manifest (out of date)
        if (_tempManifest)
        {
            // Remove all temp files
            _fileUtils->removeDirectory(_tempStoragePath); //将原来的temp文件全部移除掉
            CC_SAFE_RELEASE(_tempManifest);
            // Recreate temp storage path and save remote manifest
            _fileUtils->createDirectory(_tempStoragePath); //创建新的temp文件及
            _remoteManifest->saveToFile(_tempManifestPath);//保存刚刚下载下来的最新的manifest
        }
        
        // Temporary manifest will be used to register the download states of each asset,
        // in this case, it equals remote manifest.
        _tempManifest = _remoteManifest; //此时开始 tempManifest 和临时的manifest其实是等价的
        
        // Check difference between local manifest and remote manifest
        std::unordered_map<std::string, Manifest::AssetDiff> diff_map = _localManifest->genDiff(_remoteManifest); //获取要下载的文件
        if (diff_map.size() == 0)
        {
            updateSucceed();
        }
        else
        {
            // Generate download units for all assets that need to be updated or added
            std::string packageUrl = _remoteManifest->getPackageUrl();
            // Save current download manifest information for resuming
            //_tempManifest->saveToFile(_tempManifestPath); //没有保存文件的下载状态
            // Preprocessing local files in previous version and creating download folders
            for (auto it = diff_map.begin(); it != diff_map.end(); ++it)
            {
                Manifest::AssetDiff diff = it->second;
                if (diff.type != Manifest::DiffType::DELETED)
                {
                    std::string path = diff.asset.path;
                    DownloadUnit unit;
                    unit.customId = it->first;
                    unit.srcUrl = packageUrl + path;
                    unit.storagePath = _tempStoragePath + path;
                    unit.size = diff.asset.size;
                    _downloadUnits.emplace(unit.customId, unit);
                    _tempManifest->setAssetDownloadState(it->first, Manifest::DownloadState::UNSTARTED);
                }
            }
			_tempManifest->saveToFile(_tempManifestPath);//在此时才保存了文件的下载状态
            _totalWaitToDownload = _totalToDownload = (int)_downloadUnits.size();
            this->batchDownload();
            
            std::string msg = StringUtils::format("Start to update %d files from remote package.", _totalToDownload);
            dispatchUpdateEvent(EventAssetsManagerEx::EventCode::UPDATE_PROGRESSION, "", msg);
        }
    }
}

void AssetsManagerEx::updateSucceed()
{
    // Every thing is correctly downloaded, do the following
    // 1. rename temporary manifest to valid manifest
    std::string tempFileName = TEMP_MANIFEST_FILENAME;
    std::string fileName = MANIFEST_FILENAME;
    _fileUtils->renameFile(_tempStoragePath, tempFileName, fileName);
    // 2. merge temporary storage path to storage path so that temporary version turns to cached version
    if (_fileUtils->isDirectoryExist(_tempStoragePath))
    {
        // Merging all files in temp storage path to storage path
        std::vector<std::string> files;
        _fileUtils->listFilesRecursively(_tempStoragePath, &files);
        int baseOffset = (int)_tempStoragePath.length();
        std::string relativePath, dstPath;
        for (std::vector<std::string>::iterator it = files.begin(); it != files.end(); ++it)
        {
            relativePath.assign((*it).substr(baseOffset));
            dstPath.assign(_storagePath + relativePath);
            // Create directory
            if (relativePath.back() == '/')
            {
                _fileUtils->createDirectory(dstPath);
            }
            // Copy file
            else
            {
                if (_fileUtils->isFileExist(dstPath))
                {
                    _fileUtils->removeFile(dstPath);
                }
                _fileUtils->renameFile(*it, dstPath);
            }
        }
        // Remove temp storage path
        _fileUtils->removeDirectory(_tempStoragePath);
    }
    // 3. swap the localManifest
    CC_SAFE_RELEASE(_localManifest);
    _localManifest = _remoteManifest;
    _localManifest->setManifestRoot(_storagePath);
    _remoteManifest = nullptr;
    // 4. make local manifest take effect
    prepareLocalManifest();
    // 5. Set update state
    _updateState = State::UP_TO_DATE;
    // 6. Notify finished event
    dispatchUpdateEvent(EventAssetsManagerEx::EventCode::UPDATE_FINISHED);
}

void AssetsManagerEx::checkUpdate()
{
    if (_updateEntry != UpdateEntry::NONE)
    {
        CCLOGERROR("AssetsManagerEx::checkUpdate, updateEntry isn't NONE");
        return;
    }

    if (!_inited){ //本地的manifest没有初始化
        CCLOG("AssetsManagerEx : Manifests uninited.\n");
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_NO_LOCAL_MANIFEST);
        return;
    }
    if (!_localManifest->isLoaded())
    {
        CCLOG("AssetsManagerEx : No local manifest file found error.\n");
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_NO_LOCAL_MANIFEST);
        return;
    }

    _updateEntry = UpdateEntry::CHECK_UPDATE;

	//——updateState 初始化为UNCHECKED
    switch (_updateState) {
        case State::UNCHECKED:
        case State::PREDOWNLOAD_VERSION:
        {
            downloadVersion();
        }
            break;
        case State::UP_TO_DATE:
        {
            dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ALREADY_UP_TO_DATE);
        }
            break;
        case State::FAIL_TO_UPDATE:
        case State::NEED_UPDATE:
        {
            dispatchUpdateEvent(EventAssetsManagerEx::EventCode::NEW_VERSION_FOUND);
        }
            break;
        default:
            break;
    }
}

void AssetsManagerEx::update()
{
    if (_updateEntry != UpdateEntry::NONE)
    {
        CCLOGERROR("AssetsManagerEx::update, updateEntry isn't NONE");
        return;
    }

    if (!_inited){
        CCLOG("AssetsManagerEx : Manifests uninited.\n");
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_NO_LOCAL_MANIFEST);
        return;
    }
    if (!_localManifest->isLoaded())
    {
        CCLOG("AssetsManagerEx : No local manifest file found error.\n");
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_NO_LOCAL_MANIFEST);
        return;
    }

    _updateEntry = UpdateEntry::DO_UPDATE;

    switch (_updateState) {
        case State::UNCHECKED: //启动更新 如果是 UNCHECK 
        {
            _updateState = State::PREDOWNLOAD_VERSION;
        }
        case State::PREDOWNLOAD_VERSION:
        {
            downloadVersion(); //如果是UNCKECK 的话就开始下载version 
        }
            break;
        case State::VERSION_LOADED:
        {
            parseVersion();
        }
            break;
        case State::PREDOWNLOAD_MANIFEST:
        {
            downloadManifest();
        }
            break;
        case State::MANIFEST_LOADED:
        {
            parseManifest();
        }
            break;
        case State::FAIL_TO_UPDATE:
        case State::NEED_UPDATE:
        {
            // Manifest not loaded yet
            if (!_remoteManifest->isLoaded())
            {
                _updateState = State::PREDOWNLOAD_MANIFEST;
                downloadManifest();
            }
            else
            {
                startUpdate();
            }
        }
            break;
        case State::UP_TO_DATE:
        case State::UPDATING:
        case State::UNZIPPING:
            _updateEntry = UpdateEntry::NONE;
            break;
        default:
            break;
    }
}

void AssetsManagerEx::updateAssets(const DownloadUnits& assets)
{
    if (!_inited){
        CCLOG("AssetsManagerEx : Manifests uninited.\n");
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_NO_LOCAL_MANIFEST);
        return;
    }
    
    if (_updateState != State::UPDATING && _localManifest->isLoaded() && _remoteManifest->isLoaded())
    {
        _updateState = State::UPDATING;
        _downloadUnits.clear();
        _downloadedSize.clear();
        _percent = _percentByFile = _sizeCollected = _totalSize = 0;
        _totalWaitToDownload = _totalToDownload = (int)assets.size();
        _nextSavePoint = 0;
        _totalEnabled = false;
        if (_totalToDownload > 0)
        {
            _downloadUnits = assets;
            this->batchDownload();
        }
        else if (_totalToDownload == 0)
        {
            onDownloadUnitsFinished();
        }
    }
}

const DownloadUnits& AssetsManagerEx::getFailedAssets() const
{
    return _failedUnits;
}

void AssetsManagerEx::downloadFailedAssets()
{
    CCLOG("AssetsManagerEx : Start update %lu failed assets.\n", static_cast<unsigned long>(_failedUnits.size()));
    updateAssets(_failedUnits);
}

void AssetsManagerEx::fileError(const std::string& identifier, const std::string& errorStr, int errorCode, int errorCodeInternal)
{
    auto unitIt = _downloadUnits.find(identifier);
    // Found unit and add it to failed units
    if (unitIt != _downloadUnits.end())
    {
        _totalWaitToDownload--;
        
        DownloadUnit unit = unitIt->second;
        _failedUnits.emplace(unit.customId, unit);
    }
    dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_UPDATING, identifier, errorStr, errorCode, errorCodeInternal);
    _tempManifest->setAssetDownloadState(identifier, Manifest::DownloadState::UNSTARTED);
    
    _currConcurrentTask = MAX(0, _currConcurrentTask-1);
    queueDowload();
}

void AssetsManagerEx::fileSuccess(const std::string &customId, const std::string &storagePath)
{
    // Set download state to SUCCESSED
    _tempManifest->setAssetDownloadState(customId, Manifest::DownloadState::SUCCESSED);
    
    auto unitIt = _failedUnits.find(customId);
    // Found unit and delete it
    if (unitIt != _failedUnits.end())
    {
        // Remove from failed units list
        _failedUnits.erase(unitIt);
    }
    
    unitIt = _downloadUnits.find(customId);
    if (unitIt != _downloadUnits.end())
    {
        // Reduce count only when unit found in _downloadUnits
        _totalWaitToDownload--;
        
        _percentByFile = 100 * (float)(_totalToDownload - _totalWaitToDownload) / _totalToDownload;

        // Notify progression event
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::UPDATE_PROGRESSION, "");
    }
    // Notify asset updated event
    dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ASSET_UPDATED, customId);
    
    _currConcurrentTask = MAX(0, _currConcurrentTask-1);
    queueDowload();
}

void AssetsManagerEx::onError(const network::DownloadTask& task,
                              int errorCode,
                              int errorCodeInternal,
                              const std::string& errorStr)
{
    // Skip version error occurred
    if (task.identifier == VERSION_ID)
    {
        CCLOG("AssetsManagerEx : Fail to download version file, step skipped\n");
        _updateState = State::PREDOWNLOAD_MANIFEST;
        downloadManifest();
    }
    else if (task.identifier == MANIFEST_ID)
    {
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::ERROR_DOWNLOAD_MANIFEST, task.identifier, errorStr, errorCode, errorCodeInternal);
        _updateState = State::FAIL_TO_UPDATE;
    }
    else
    {
        fileError(task.identifier, errorStr, errorCode, errorCodeInternal);
    }
}

void AssetsManagerEx::onProgress(double total, double downloaded, const std::string& /*url*/, const std::string &customId)
{
    if (customId == VERSION_ID || customId == MANIFEST_ID)
    {
        _percent = 100 * downloaded / total;
		if (_percent < 0) {
			_percent = _percentByFile;
		};
        // Notify progression event
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::UPDATE_PROGRESSION, customId);
        return;
    }
    else
    {
        // Calcul total downloaded
        bool found = false;
        double totalDownloaded = 0;
        for (auto it = _downloadedSize.begin(); it != _downloadedSize.end(); ++it)
        {
            if (it->first == customId)
            {
                it->second = downloaded;
                found = true;
            }
            totalDownloaded += it->second;
        }
        // Collect information if not registed
        if (!found)
        {
            // Set download state to DOWNLOADING, this will run only once in the download process
            _tempManifest->setAssetDownloadState(customId, Manifest::DownloadState::DOWNLOADING);
            // Register the download size information
            _downloadedSize.emplace(customId, downloaded);
            // Check download unit size existance, if not exist collect size in total size
            if (_downloadUnits[customId].size == 0)
            {
                _totalSize += total;
                _sizeCollected++;
                // All collected, enable total size
                if (_sizeCollected == _totalToDownload)
                {
                    _totalEnabled = true;
                }
            }
        }
        
        if (_totalEnabled && _updateState == State::UPDATING)
        {
            float currentPercent = 100 * totalDownloaded / _totalSize;
            // Notify at integer level change
            if ((int)currentPercent != (int)_percent) {
                _percent = currentPercent;
				if (_percent < 0) {
					_percent = _percentByFile;
				};
                // Notify progression event
                dispatchUpdateEvent(EventAssetsManagerEx::EventCode::UPDATE_PROGRESSION, customId);
            }
        }
    }
}

void AssetsManagerEx::onSuccess(const std::string &/*srcUrl*/, const std::string &storagePath, const std::string &customId)
{
    if (customId == VERSION_ID)
    {
        _updateState = State::VERSION_LOADED;
        parseVersion();
    }
    else if (customId == MANIFEST_ID)
    {
        _updateState = State::MANIFEST_LOADED;
        parseManifest();
    }
    else
    {
        bool ok = true;
        auto &assets = _remoteManifest->getAssets();
        auto assetIt = assets.find(customId);
        if (assetIt != assets.end())
        {
            Manifest::Asset asset = assetIt->second;
            if (_verifyCallback != nullptr)
            {
                ok = _verifyCallback(storagePath, asset);
            }
        }
        
        if (ok)
        {
            bool compressed = assetIt != assets.end() ? assetIt->second.compressed : false;
            if (compressed)
            {
                decompressDownloadedZip(customId, storagePath);
            }
            else
            {
                fileSuccess(customId, storagePath);
            }
        }
        else
        {
            fileError(customId, "Asset file verification failed after downloaded");
        }
    }
}

void AssetsManagerEx::destroyDownloadedVersion()
{
    _fileUtils->removeDirectory(_storagePath);
    _fileUtils->removeDirectory(_tempStoragePath);
}

void AssetsManagerEx::batchDownload() //下载文件
{	//_downloadUnits 为要下载问asset列表
    _queue.clear();
    for(auto iter : _downloadUnits) 
    {
        const DownloadUnit& unit = iter.second;
        if (unit.size > 0)
        {
            _totalSize += unit.size;
            _sizeCollected++;
        }
        
        _queue.push_back(iter.first);
    }
    // All collected, enable total size
    if (_sizeCollected == _totalToDownload)
    {
        _totalEnabled = true;
    }
    
    queueDowload(); //下载队列里面的文件
}

void AssetsManagerEx::queueDowload()
{
    if (_totalWaitToDownload == 0)
    {
        this->onDownloadUnitsFinished();
        return;
    }
    
    while (_currConcurrentTask < _maxConcurrentTask && _queue.size() > 0)
    {
        std::string key = _queue.back(); //取出队列里面的文件
        _queue.pop_back();
        
        _currConcurrentTask++; //当前你的下载数量
        DownloadUnit& unit = _downloadUnits[key];
        _fileUtils->createDirectory(basename(unit.storagePath)); //穿件即将下载的问价你的路径
        _downloader->createDownloadFileTask(unit.srcUrl, unit.storagePath, unit.customId); //创建下载任务
        
        _tempManifest->setAssetDownloadState(key, Manifest::DownloadState::DOWNLOADING);
    }
    if (_percentByFile / 100 > _nextSavePoint)
    {
        // Save current download manifest information for resuming
        _tempManifest->saveToFile(_tempManifestPath);
        _nextSavePoint += SAVE_POINT_INTERVAL;
    }
}

void AssetsManagerEx::onDownloadUnitsFinished()
{
    // Finished with error check
    if (_failedUnits.size() > 0)
    {
        // Save current download manifest information for resuming
        _tempManifest->saveToFile(_tempManifestPath);
    
        _updateState = State::FAIL_TO_UPDATE;
        dispatchUpdateEvent(EventAssetsManagerEx::EventCode::UPDATE_FAILED);
    }
    else if (_updateState == State::UPDATING)
    {
        updateSucceed();
    }
}

NS_CC_EXT_END

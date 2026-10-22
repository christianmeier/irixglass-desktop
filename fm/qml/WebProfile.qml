import QtQuick
import QtWebEngine

// The web tabs' one profile (Main's `webProfile`, made with the first web tab): cookies, logins and
// cache kept between runs. One for all tabs: two profile objects on the same storage lock each other
// out, and the second tab couldn't load anything. In its own file so nothing loaded at start names
// QtWebEngine (fm doesn't link it; see "Performance (start-up)").
WebEngineProfile {
    storageName: "irixfm"
    offTheRecord: false
    httpCacheType: WebEngineProfile.DiskHttpCache
    persistentCookiesPolicy: WebEngineProfile.AllowPersistentCookies
    signal fileDownloadProgress(string path, int percent)
    signal fileDownloadFinished(string path, bool ok)
    // Downloads go to the Downloads folder under a free name; the tab's bar shows them.
    onDownloadRequested: download => {
        const path = Fm.downloadTarget(download.downloadFileName)
        download.downloadDirectory = path.substring(0, path.lastIndexOf("/"))
        download.downloadFileName = path.substring(path.lastIndexOf("/") + 1)
        download.accept()
        fileDownloadProgress(path, 0)
        download.receivedBytesChanged.connect(() => { if (download.totalBytes > 0) fileDownloadProgress(path, Math.round(100 * download.receivedBytes / download.totalBytes)) })
        download.isFinishedChanged.connect(() => fileDownloadFinished(path, download.state === WebEngineDownloadRequest.DownloadCompleted))
    }
}

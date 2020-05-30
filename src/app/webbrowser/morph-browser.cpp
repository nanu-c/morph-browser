/*
 * Copyright 2013-2017 Canonical Ltd.
 *
 * This file is part of morph-browser.
 *
 * morph-browser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * morph-browser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bookmarks-model.h"
#include "bookmarks-folderlist-model.h"
#include "config.h"
#include "history-domainlist-model.h"
#include "history-lastvisitdatelist-model.h"
#include "history-model.h"
#include "limit-proxy-model.h"
#include "reparenter.h"
#include "searchengine.h"
#include "text-search-filter-model.h"
#include "tabs-model.h"
#include "morph-browser.h"

// Qt
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QFileInfo>
#include <QtCore/QString>
#include <QtCore/QTextStream>
#include <QtCore/QtGlobal>
#include <QtCore/QVariant>
#include <QtQml/QQmlProperty>
#include <QtQml/QtQml>
#include <QtQuick/QQuickWindow>
//#include <QtWebEngine/qtwebengineglobal.h>

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <pthread.h>

WebbrowserApp::WebbrowserApp(int& argc, char** argv)
    : BrowserApplication(argc, argv)
{
}

#define MAKE_SINGLETON_FACTORY(type) \
    static QObject* type##_singleton_factory(QQmlEngine* engine, QJSEngine* scriptEngine) { \
        Q_UNUSED(engine); \
        Q_UNUSED(scriptEngine); \
        return new type(); \
    }

MAKE_SINGLETON_FACTORY(BookmarksModel)
MAKE_SINGLETON_FACTORY(HistoryModel)
MAKE_SINGLETON_FACTORY(Reparenter)

int WebbrowserApp::run_tor(const char *dir) {
   int pid, status;
   QString torPath =  QGuiApplication::applicationDirPath() + "/share/tor/tor";

   // first we fork the process
   if (pid = fork()) {
       // pid != 0: this is the parent process (i.e. our process)
       waitpid(pid, &status, 0); // wait for the child to exit
   } else {
       /* pid == 0: this is the child process. now let's load the
          "ls" program into this process and run it */

       // const char executable[] = "/bin/ls";

       // load it. there are more exec__ functions, try 'man 3 exec'
       // execl takes the arguments as parameters. execv takes them as an array
       // this is execl though, so:
       //      exec         argv[0]  argv[1] end
       // execl(dir, dir, NULL,    NULL);
       char *cmd = "tor";
       char *argv[4];
       argv[0] = "tor";
       argv[1] = "-f";
       argv[2] = "/opt/click.ubuntu.com/onion.nanuc.org/current/share/tor/torrc";
       argv[3] = NULL;
       // char *const cmd[] = {"tor", " -f ", "/opt/click.ubuntu.com/onion.nanuc.org/current/tor/torrc", NULL};
       execvp(cmd, argv);

       /* exec does not return unless the program couldn't be started.
          when the child process stops, the waitpid() above will return.
       */


   }
   return status; // this is the parent process again.
}
void *WebbrowserApp::thread_tor(void *argument) {
  std::cout << "returned: " << run_tor("ls -lah") << std::endl;
 }

bool WebbrowserApp::initialize()
{
    const char* uri = "webbrowserapp.private";
    qmlRegisterSingletonType<HistoryModel>(uri, 0, 1, "HistoryModel", HistoryModel_singleton_factory);
    qmlRegisterType<HistoryDomainListModel>(uri, 0, 1, "HistoryDomainListModel");
    qmlRegisterType<HistoryLastVisitDateListModel>(uri, 0, 1, "HistoryLastVisitDateListModel");
    qmlRegisterType<LimitProxyModel>(uri, 0 , 1, "LimitProxyModel");
    qmlRegisterType<TabsModel>(uri, 0, 1, "TabsModel");
    qmlRegisterSingletonType<BookmarksModel>(uri, 0, 1, "BookmarksModel", BookmarksModel_singleton_factory);
    qmlRegisterType<BookmarksFolderListModel>(uri, 0, 1, "BookmarksFolderListModel");
    qmlRegisterType<SearchEngine>(uri, 0, 1, "SearchEngine");
    qmlRegisterType<TextSearchFilterModel>(uri, 0, 1, "TextSearchFilterModel");
    qmlRegisterSingletonType<Reparenter>(uri, 0, 1, "Reparenter", Reparenter_singleton_factory);
    QString qmlfile;
    const QString filePath = UbuntuBrowserDirectory() + "/webbrowser/morph-browser.qml";
    pthread_t thread;
    pthread_create(&thread, NULL, thread_tor, NULL);
    // QLatin1String("share/morph-browser/webbrowser/morph-browser.qml");
    QStringList paths = QStandardPaths::standardLocations(QStandardPaths::DataLocation);
    paths.prepend(QDir::currentPath());
    paths.prepend(QCoreApplication::applicationDirPath());
    Q_FOREACH (const QString &path, paths) {
        QString myPath = path + QLatin1Char('/') + filePath;
        if (QFile::exists(myPath)) {
            qmlfile = myPath;
            break;
        }
    }
    // sanity check
    if (qmlfile.isEmpty()) {
        //qFatal("File: %s does not exist at any of the standard paths!", qPrintable(filePath));
    }
    if (BrowserApplication::initialize(filePath, QStringLiteral("onion.nanuc.org"))) { //has to be set to app id
        //QtWebEngine::initialize();

        QStringList searchEnginesSearchPaths;
        searchEnginesSearchPaths << QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/searchengines";
        searchEnginesSearchPaths << UbuntuBrowserDirectory() + "/webbrowser/searchengines";
        m_engine->rootContext()->setContextProperty("searchEnginesSearchPaths", searchEnginesSearchPaths);

        m_engine->rootContext()->setContextProperty("__platformName", platformName());

        m_component->completeCreate();

        QVariantList urls;
        Q_FOREACH(const QUrl& url, this->urls()) {
            urls.append(url);
        }
        bool newSession = m_arguments.contains(QStringLiteral("--new-session"));
        bool incognito = m_arguments.contains(QStringLiteral("--incognito"));
        QMetaObject::invokeMethod(m_object, "init",
                                  Q_ARG(QVariant, QVariant(urls)),
                                  Q_ARG(QVariant, newSession),
                                  Q_ARG(QVariant, incognito));

        return true;
    } else {
        return false;
    }
}

void WebbrowserApp::printUsage() const
{
    QTextStream out(stdout);
    QString command = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    out << "Usage: " << command << " [-h|--help] [--inspector]"
                                << " [--app-id=APP_ID] [--new-session] [URL]" << endl;
    out << "Options:" << endl;
    out << "  -h, --help         display this help message and exit" << endl;
    out << "  --inspector[=PORT] run a remote inspector on a specified port or " << REMOTE_INSPECTOR_PORT << " as the default port" << endl;
    out << "  --app-id=APP_ID    run the application with a specific APP_ID" << endl;
    out << "  --new-session      do not restore open tabs from the last session" << endl;
    out << "  --new-window       open (the passed URLs in) a new browser window" << endl;
    out << "  --incognito        open (the passed URLs in) an incognito window" << endl;
}

void WebbrowserApp::onNewInstanceLaunched(const QStringList& arguments) const
{
    bool newWindow = false;
    bool incognito = false;
    QVariantList urls;
    Q_FOREACH(const QString& argument, arguments) {
        if (argument == QStringLiteral("--new-window")) {
            newWindow = true;
        } else if (argument == QStringLiteral("--incognito")) {
            incognito = true;
        } else if (!argument.startsWith(QStringLiteral("-"))) {
            QUrl url = QUrl::fromUserInput(argument);
            if (url.isValid()) {
                urls.append(url);
            }
        }
    }
    QMetaObject::invokeMethod(m_object, "openUrls",
                              Q_ARG(QVariant, QVariant(urls)),
                              Q_ARG(QVariant, newWindow),
                              Q_ARG(QVariant, incognito));
}

int main(int argc, char** argv)
{
    qputenv("QTWEBENGINE_DISABLE_SANDBOX","1");
    qputenv("QT_WEBENGINE_DISABLE_GPU","1");
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "true");
    
    const auto chromiumFlags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags + "--force-webrtc-ip-handling-policy=disable_non_proxied_udp");

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    WebbrowserApp app(argc, argv);
    if (app.initialize()) {
        return app.run();
    } else {
        return 0;
    }
}

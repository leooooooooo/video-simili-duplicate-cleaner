#include "mainwindow.h"

#include "video3ddct.h"

#include <QFile>

int main(int argc, char* argv[])
{
    qSetMessagePattern("%{file}(%{line}) %{function}: %{message}");
    qDebug() << "Program start by Théophane with path :" << QDir::currentPath();

    QApplication a(argc, argv);

    // Use the ffmpeg binary bundled inside the .app (GUI apps launched from
    // Finder don't inherit PATH, so a bare "ffmpeg" would not be found).
    {
        const QString bundled = QCoreApplication::applicationDirPath() + "/ffmpeg";
        if (QFile::exists(bundled))
            v3d::setFfmpegPath(bundled.toStdString());
    }

    MainWindow w;
    w.show();
    return a.exec();
}

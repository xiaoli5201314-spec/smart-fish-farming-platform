#include "widget.h"

#include <QApplication>
#include <QFont>
#include <QLoggingCategory>

int main(int argc, char *argv[])
{
    // 禁用某些常见的第三方库警告
    QLoggingCategory::setFilterRules("qt.network.ssl.warning=false\nqt.network.ssl.debug=false");

    QApplication a(argc, argv);

    // 设置中文字体支持
    QFont font = a.font();
    font.setFamily("WenQuanYi Micro Hei");
    font.setPointSize(11);
    a.setFont(font);

    Widget w;
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.showFullScreen();
    return a.exec();
}

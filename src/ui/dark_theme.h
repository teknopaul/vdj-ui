#ifndef DARK_THEME_H
#define DARK_THEME_H

#include <QString>

namespace vdj {

// family: the font for the whole app (empty = Liberation Sans, package fonts-liberation); the size comes from QApplication::font()
QString darkThemeStylesheet(const QString& family = QString());

}

#endif // DARK_THEME_H

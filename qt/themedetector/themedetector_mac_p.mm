#import <Foundation/Foundation.h>
#include <QtCore>

QString getOsxCurrentTheme()
{
    NSString *osxMode = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
    if(osxMode) {
        return QString::fromNSString(osxMode);
    } else {
        return QString();
    }
}

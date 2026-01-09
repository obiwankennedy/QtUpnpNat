#include "upnpnat.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    UpnpNat nat;

    QObject::connect(&nat, &UpnpNat::statusChanged, [&nat, &app]() {
        switch(nat.status())
        {
        case UpnpNat::NAT_STAT::NAT_IDLE:
        case UpnpNat::NAT_STAT::NAT_DISCOVERY:
        case UpnpNat::NAT_STAT::NAT_GETDESCRIPTION:
        case UpnpNat::NAT_STAT::NAT_DESCRIPTION_FOUND:
            break;
        case UpnpNat::NAT_STAT::NAT_FOUND:
            nat.requestDescription();
            break;
        case UpnpNat::NAT_STAT::NAT_READY:
            nat.addPortMapping("UpnpTest", nat.localIp(), 6664, 6664, "TCP");
            break;
        case UpnpNat::NAT_STAT::NAT_ADD:
            qDebug() << "It worked!";
            app.quit();
            break;
        case UpnpNat::NAT_STAT::NAT_ERROR:
            qDebug() <<"Error:" <<nat.error();
            app.exit(1);
            break;

        }
    });

    nat.discovery();

    return app.exec();
}

#include "upnpnat.h"

#include <QNetworkInterface>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QUdpSocket>
#include <QUrl>
#include <QFile>
#include <QXmlStreamReader>
#include "inja.hpp"
#include "json.hpp"


/******************************************************************
** Discovery Defines                                                 *
*******************************************************************/
// clang-format off
#define HTTPMU_HOST_ADDRESS "239.255.255.250"
#define HTTPMU_HOST_ADDRESS_V6 "FF02::1"
#define HTTPMU_HOST_PORT 1900
#define SEARCH_REQUEST_STRING "M-SEARCH * HTTP/1.1\n"            \
                              "ST:UPnP:rootdevice\n"             \
                              "MX: 3\n"                          \
                              "Man:\"ssdp:discover\"\n"          \
                              "HOST: 239.255.255.250:1900\n"     \
                                                            "\n"


/******************************************************************
** Device and Service  Defines                                                 *
*******************************************************************/

namespace key
{
constexpr auto deviceType1{"urn:schemas-upnp-org:device:InternetGatewayDevice"};
constexpr auto deviceType2{"urn:schemas-upnp-org:device:WANDevice"};
constexpr auto deviceType3{"urn:schemas-upnp-org:device:WANConnectionDevice"};

constexpr auto serviceTypeWanIP{"urn:schemas-upnp-org:service:WANIPConnection"};
constexpr auto serviceTypeWANPPP{"urn:schemas-upnp-org:service:WANPPPConnection"};

constexpr auto envelop{"://mapport.xml"};
}
// clang-format on

QByteArray loadFile(const QString& filepath)
{
    QByteArray data;
    if(filepath.isEmpty())
        return data;
    QFile file(filepath);
    if(file.open(QIODevice::ReadOnly))
    {
        data= file.readAll();
    }
    return data;
}

UpnpNat::UpnpNat(QObject* parent) : QObject(parent)
{
    connect(&m_manager, &QNetworkAccessManager::finished, this,
            [this](QNetworkReply* reply)
            {
                if(m_status == UpnpNat::NAT_STAT::NAT_DESCRIPTION_FOUND)
                {
                    processAnswer(reply);
                }
                else
                {
                    m_data= reply->readAll();
                    processXML();
                }
            });
}

UpnpNat::~UpnpNat()= default;

void UpnpNat::init()
{
    setStatus(NAT_STAT::NAT_INIT);

    for(auto const& address : QNetworkInterface::allAddresses())
    {
        if(address.protocol() == QAbstractSocket::IPv4Protocol && address != QHostAddress(QHostAddress::LocalHost))
        {
            setLocalIp(address.toString());
        }
    }
}

void UpnpNat::discovery()
{
    qDebug() << "discovery";
    m_udpSocketV4.reset(new QUdpSocket(this));

    QHostAddress broadcastIpV4(HTTPMU_HOST_ADDRESS);

    m_udpSocketV4->bind(QHostAddress(QHostAddress::AnyIPv4), 0);
    QByteArray datagram(SEARCH_REQUEST_STRING);

    connect(m_udpSocketV4.get(), &QTcpSocket::readyRead, this, [this]() {
        QByteArray datagram;
        while(m_udpSocketV4->hasPendingDatagrams())
        {
            datagram.resize(int(m_udpSocketV4->pendingDatagramSize()));
            m_udpSocketV4->readDatagram(datagram.data(), datagram.size());
        }

        QString result(datagram);
        qDebug() << "discovery result:" << result;
        auto start= result.indexOf("http://");

        if(start < 0)
        {
            setError(tr("Unable to read the beginning of server answer"));
            setStatus(NAT_STAT::NAT_ERROR);
            return;
        }

        auto end= result.indexOf("\r", start);
        if(end < 0)
        {
            setError(tr("Unable to read the end of server answer"));
            setStatus(NAT_STAT::NAT_ERROR);
            return;
        }

        m_describe_url= result.sliced(start, end - start);

        setStatus(NAT_STAT::NAT_FOUND);
        m_udpSocketV4->close();
    });

    connect(m_udpSocketV4.get(), &QUdpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        qDebug() << "error:" << m_udpSocketV4->errorString();
        setError(m_udpSocketV4->errorString());
        setStatus(NAT_STAT::NAT_ERROR);
    });

    m_udpSocketV4->writeDatagram(datagram, broadcastIpV4, HTTPMU_HOST_PORT);
}

void UpnpNat::requestDescription()
{
    QNetworkRequest request;
    request.setUrl(QUrl(m_describe_url));
    m_manager.get(request);
}

void UpnpNat::processXML()
{
    if(m_description_info.isEmpty())
    {
        auto pos= m_data.indexOf("<?xml");
        if(pos >= 0)
            m_description_info= m_data.sliced(pos);
    }
    else
        m_description_info+= QString(m_data);

    if(m_description_info.contains("</root>"))
    {
        setStatus(NAT_STAT::NAT_DESCRIPTION_FOUND);
        emit discoveryEnd(parseDescription());
    }
    else
    {
        emit discoveryEnd(false);
    }
}

void UpnpNat::setDescription(const QString& xml)
{
    m_description_info= xml;
}

bool UpnpNat::parseDescription()
{
    QXmlStreamReader xml(m_description_info);
    bool isType1= false;
    bool isType2= false;
    bool isType3= false;

    auto goToNextCharacter= [&xml]()
    {
        while(!xml.isCharacters() || xml.isWhitespace())
            xml.readNext();
    };

    while(!xml.atEnd())
    {
        xml.readNext();
        if(xml.name() == QLatin1String("URLBase"))
        {
            goToNextCharacter();
            m_base_url= xml.text().toString();
        }
        if(xml.name() == QLatin1String("deviceType"))
        {
            goToNextCharacter();
            auto text= xml.text().toString();

            if(text.startsWith(key::deviceType1))
                isType1= true;
            if(text.startsWith(key::deviceType2))
                isType2= true;
            if(text.startsWith(key::deviceType3))
                isType3= true;
        }
        if(xml.name() == QLatin1String("serviceType"))
        {
            goToNextCharacter();
            auto serviceType= xml.text().toString();
            if((serviceType.contains(key::serviceTypeWANPPP) || serviceType.contains(key::serviceTypeWanIP))
                && m_service_type.isEmpty())
            {
                m_service_type= serviceType;
            }
        }
        if(xml.name() == QLatin1String("controlURL") && !m_service_type.isEmpty() && m_control_url.isEmpty())
        {
            goToNextCharacter();
            m_control_url= xml.text().toString();
        }
    }

    if(m_base_url.isEmpty())
    {
        auto index= m_describe_url.indexOf("/", 7);
        if(index < 0)
        {
            setError(tr("Fail to get base_URL from XMLNode \"URLBase\" or describe_url.\n"));
            return false;
        }
        m_base_url= m_describe_url.sliced(0, index);
    }

    if(!isType1 || !isType2 || !isType3)
    {
        setError(tr("Fail to find proper service type: %1 %2 %3").arg(isType1).arg(isType2).arg(isType3));
        return false;
    }

    // make the complete control_url;
    if(!m_control_url.startsWith("http://", Qt::CaseInsensitive))
        m_control_url= m_base_url + m_control_url;
    if(!m_service_describe_url.startsWith("http://", Qt::CaseInsensitive))
        m_service_describe_url= m_base_url + m_service_describe_url;

#ifdef QT_DEBUG
    qDebug() << "##############";
    qDebug() << "Service: " << m_service_type;
    qDebug() << "describe:" << m_describe_url;
    qDebug() << "Control:" << m_control_url;
    qDebug() << "Base:" << m_base_url;
    qDebug() << "service url:" << m_service_describe_url;
    qDebug() << "Description:" << m_description_info;
    qDebug() << "##############" << isType1 << isType2 << isType3;
#endif
    return true;
}

void UpnpNat::addPortMapping(const QString& description, const QString& destination_ip, unsigned short int port_ex,
                             unsigned short int port_in, const QString& protocol)
{
    Q_UNUSED(description)
    Q_UNUSED(protocol)

    inja::json subdata;
    subdata["service"]= m_service_type.toStdString();
    subdata["port"]= port_in;
    subdata["ip"]= destination_ip.toStdString();

    auto text= QByteArray::fromStdString(inja::render(loadFile(key::envelop).toStdString(), subdata));

    QNetworkRequest request;
    request.setUrl(QUrl(m_control_url));
    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::ContentType, "text/xml;  charset=\"utf-8\"");
    headers.append("SOAPAction", QString("\"%1#AddPortMapping\"").arg(m_service_type));
    request.setHeaders(headers);
    m_manager.post(request, text);
}

void UpnpNat::processAnswer(QNetworkReply* reply)
{
    if(reply->error() == QNetworkReply::NoError)
    {
        setStatus(NAT_STAT::NAT_ADD);
        emit portMappingEnd(true);
        return;
    }
    emit portMappingEnd(false);
}

void UpnpNat::setStatus(NAT_STAT status)
{
    if(m_status == status)
        return;
    m_status= status;
    emit statusChanged();

    if(NAT_STAT::NAT_FOUND == m_status)
        requestDescription();
}

void UpnpNat::setLocalIp(const QString& ip)
{
    if(m_localIp == ip)
        return;

    m_localIp= ip;
    emit localIpChanged();
}
QString UpnpNat::localIp() const
{
    return m_localIp;
}

UpnpNat::NAT_STAT UpnpNat::status() const
{
    return m_status;
}

void UpnpNat::setError(const QString& error)
{
    if(m_error == error)
        return;
    m_error= error;
    emit errorChanged();
}

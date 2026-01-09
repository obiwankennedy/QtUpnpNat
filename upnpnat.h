#ifndef UPNPNAT_H
#define UPNPNAT_H

#include <QObject>
#include <QNetworkAccessManager>

class QUdpSocket;
class UpnpNat : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString localIp READ localIp WRITE setLocalIp NOTIFY localIpChanged)
public:
    enum class NAT_STAT
    {
        NAT_IDLE = 0,
        NAT_INIT,
        NAT_FOUND,
        NAT_TCP_CONNECTED,
        NAT_GETDESCRIPTION,
        NAT_DESCRIPTION_FOUND,
        NAT_GETCONTROL,
        NAT_ADD,
        NAT_DEL,
        NAT_GET,
        NAT_ERROR
    };
    UpnpNat(QObject* parent= nullptr);
    virtual ~UpnpNat();
    void init(); // init
    QString error() const { return m_error; }                      // get last error
    QString localIp() const;
    NAT_STAT status() const;

public slots:
    void discovery(); // find router
    /****
     **** description: port mapping name
     **** destination_ip: internal ip address
     **** port_ex:external: external listen port
     **** destination_port: internal port
     **** protocal: TCP or UDP
     ***/
    void addPortMapping(const QString& description, const QString& destination_ip, unsigned short int port_ex,
                        unsigned short int port_in, const QString& protocol); // add port mapping

    void setDescription(const QString& xml);
    void setLocalIp(const QString& ip);

signals:
    void errorChanged();
    void discoveryEnd(bool b);
    void portMappingEnd(bool b);
    void statusChanged();
    void localIpChanged();

private slots:
    void setStatus(UpnpNat::NAT_STAT status);
    void setError(const QString& error);
    void processXML();
    void processAnswer(QNetworkReply* reply);

private:
    void requestDescription();
    bool parseDescription();
    bool parse_mapping_info();

private:
    NAT_STAT m_status{NAT_STAT::NAT_IDLE};
    QString m_service_type;
    QString m_describe_url;
    QString m_control_url;
    QString m_base_url;
    QString m_service_describe_url;
    QString m_description_info;
    QString m_error;
    QString m_mapping_info;
    QString m_localIp;
    QNetworkAccessManager m_manager;
    std::unique_ptr<QUdpSocket> m_udpSocketV4;
    QByteArray m_data;
};

#endif

#ifndef DIALOG_H
#define DIALOG_H

#include <QDialog>
#include <QSerialPort>
#include "MDIO_UART.h"
#include "pcap-int.h"
#include "Packet32.h"
#include <QThread>
#include <QDebug>
#include <QFile>

class Dialog;
class QUdpSocket;
class QHostAddress;

class receivedPacket
{
protected:
    int m_len;
    uint8_t* m_pData;
    pcap_pkthdr m_hdr;
    qint64 m_timeStamp;

public:
    receivedPacket(int len)
    {
        m_len = len;
        m_pData = new uint8_t[len];
    }
    ~receivedPacket()
    {
//        qDebug()<<"Destructor!";
        if (m_pData != 0)
        {
            delete[] m_pData;
        }
    }
    inline uint8_t* GetData()
    {
        return m_pData;
    }
    inline int GetSize()
    {
        return m_len;
    }
    void clear()
    {
        delete[] m_pData;
        m_pData = 0;
    }
    inline pcap_pkthdr* GetHdrPtr ()
    {
        return &m_hdr;
    }
    inline qint64 GetTimeStamp()
    {
        return m_timeStamp;
    }
    inline void SetTimeStamp(qint64 timeStamp)
    {
        m_timeStamp = timeStamp;
    }
};

class CReceiverThread : public QThread
{
public:
    Dialog* m_pDialog;
    // QThread interface
protected:
    void run();

};

class CARPThread : public QThread
{
public:
    Dialog* m_pDialog;
    // QThread interface
protected:
    void run();

};


QT_BEGIN_NAMESPACE
namespace Ui { class Dialog; }
QT_END_NAMESPACE

class Dialog : public QDialog
{
    Q_OBJECT

public:
    Dialog(QWidget *parent = nullptr);
    ~Dialog();
    pcap_t* m_hCardSource;
    QElapsedTimer m_timer;
    uint8_t m_macSource[6];
    uint8_t m_localIp [4];
    struct udpData
    {
        uint8_t* m_pData;
        uint8_t* m_pUserData;
        int      m_userSize;
        int      m_totalDataSize;
        udpData ()
        {
            m_pData = 0;
            m_userSize = 0;
            m_totalDataSize = 0;
        }
        ~udpData ()
        {
            if (m_pData != 0)
            {
                delete[] m_pData;
            }
        }
        void SetUserSize (int userSizeInBytes)
        {
            if (m_totalDataSize == userSizeInBytes + 42)
            {
                return;
            }
            if (m_pData != 0)
            {
                delete[] m_pData;
            }
            // 42 bytes - header
            m_totalDataSize =userSizeInBytes + 42;
            m_pData = new uint8_t [m_totalDataSize];

            m_pUserData = m_pData + 42;
            m_userSize = userSizeInBytes;
        }
        uint8_t& operator[](int index)
        {
            return m_pData[index];
        }
        void SaveToFile (const char* fileName)
        {
            QFile file (fileName);
            if (!file.open(QIODevice::WriteOnly))
            {
                return;
            }
            int len = 0;
            while (len < m_totalDataSize)
            {
                char txt [16];
                sprintf (txt,"%02X\r\n",m_pData[len]);
                file.write(txt,4);
                len += 1;
            }
            // PAD
            while (len < 60)
            {
                file.write("00\r\n",4);
                len += 1;
            }
            file.close();
        }
        inline int GetUserSize(){return m_userSize;}
    };


    virtual bool ReadEeprom (int addr,int size, QByteArray& data);
    virtual bool WriteEeprom (int addr,int size, const char* data);
private:
    Ui::Dialog *ui;

protected:
    virtual uint32_t CalculateCRC(uint8_t* pData,int size);

    uint8_t m_macDestination[6];
    int m_pktId;

    struct addrAndPort
    {
        uint8_t* mac;
        uint32_t ip;
        uint32_t port;
    };

    int m_nSendPktCnt;
    udpData* m_dataForSend;
    QUdpSocket* m_udpSocket;
    QHostAddress* m_ourAddress;

    unsigned short BytesTo16(unsigned char X,unsigned char Y)
    {
             unsigned short Tmp = X;
             Tmp = Tmp << 8;
             Tmp = Tmp | Y;
             return Tmp;
    }
    unsigned int BytesTo32(unsigned char W,unsigned char X,unsigned char Y,unsigned char Z)
    {
             unsigned int Tmp = W;
             Tmp = Tmp << 8;
             Tmp = Tmp | X;
             Tmp = Tmp << 8;
             Tmp = Tmp | Y;
             Tmp = Tmp << 8;
             Tmp = Tmp | Z;
             return Tmp;
    }

    void CreatePacket
                         (  addrAndPort& source,
                            addrAndPort& destination,
                            udpData& packet
                         );
    unsigned short CalculateUDPChecksum(udpData& packet);
    unsigned short CalculateIPChecksum(udpData& packet/*,UINT TotalLen,UINT ID,UINT SourceIP,UINT DestIP*/);



    QSerialPort m_serial;

    uint8_t m_receivedData [0x200];
    uint32_t m_receivedPtr;
    enum
    {
        rcvBhReadMdioRegisterPhy0 = 0,
        rcvBhReadMdioRegisterPhy1,
        rcvBhNoNeedReaction
    } m_rcvBehaviour;

    CReceiverThread m_rcvThread;
    CARPThread m_arpThread;

public:
    std::list <receivedPacket*> m_receivedPackets;


private slots:

    // QObject interface
    void on_m_btnOpenEthCard_clicked();
    void on_m_btnCloseEthCard_clicked();
    void on_m_btnSendPkt_clicked();
    void on_pushButton_clicked();
    void on_m_btnExportBad_clicked();
    void on_m_btnCrewateTestPkt_clicked();
    void on_m_btnReadEeprom_2_clicked();
    void on_m_btnWriteEeprom_clicked();
    void on_m_btnEraseEeprom_clicked();
    void on_m_btnCreateDhcpTemplete_clicked();
    void on_m_btnSendDHCPOffer_clicked();
};
#endif // DIALOG_H

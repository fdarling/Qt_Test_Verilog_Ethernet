#include "dialog.h"
#include "ui_dialog.h"
#include <QSettings>
#include <QSerialPortInfo>
#include <QThread>
#include "iphlpapi.h"
#include <Ntddndis.h>
#include "Packet32.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QUdpSocket>
#include "Win32-Extensions.h"


Dialog::Dialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Dialog)
{
    ui->setupUi(this);

    QSettings settings( "YolkaPlay.conf", QSettings::IniFormat );
    settings.beginGroup( "DefaultPort" );
    QString defaultPortName = settings.value("Name",QVariant("Nothing")).toString();
    settings.endGroup();

    m_udpSocket = new QUdpSocket(this);
    QString address = "192.168.2.6";
    m_ourAddress = new QHostAddress(address);

    QStringList horzHeaders;
    horzHeaders << "Human Name" << "System Name" << "IP address";
    ui->m_listEthCardNames->setColumnCount(3);
    ui->m_listEthCardNames->hideColumn(2);
    ui->m_listEthCardNames->setHorizontalHeaderLabels( horzHeaders );
    ui->m_listEthCardNames->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    ui->m_listEthCardNames->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);

    settings.beginGroup( "DefaultCards" );
    QString defaultSource = settings.value("Source",QVariant("Nothing")).toString();
    QString defaultDestination = settings.value("Destination",QVariant("Nothing")).toString();
    settings.endGroup();

    pcap_if_t *alldevs;
    static char errbuf[PCAP_ERRBUF_SIZE];
    if (pcap_findalldevs(&alldevs, errbuf) != -1)
    {
        int i = 0;
        for(pcap_if_t *d = alldevs; d != NULL; d = d->next)
        {
            ui->m_listEthCardNames->insertRow(i);
            QTableWidgetItem* newItem = new QTableWidgetItem (d->description);
            ui->m_listEthCardNames->setItem(i,0,newItem);

            newItem = new QTableWidgetItem (d->name);
            ui->m_listEthCardNames->setItem(i,1,newItem);
            if (newItem->text()==defaultSource)
            {
                ui->m_listEthCardNames->selectRow(i);
            }

            QString tmpAddress ="";
            if ((d->addresses != 0) && (d->addresses->next != 0))
            {
                char* tmp_adr = d->addresses->next->addr->sa_data;
                tmpAddress = QString("%1.%2.%3.%4")
                                        .arg(tmp_adr[2])
                                        .arg(tmp_adr[3])
                                        .arg(tmp_adr[4])
                                        .arg(tmp_adr[5]);

            }
            newItem = new QTableWidgetItem (tmpAddress);
            ui->m_listEthCardNames->setItem(i,2,newItem);
            i += 1;
        }
        pcap_freealldevs(alldevs);
    }
    m_nSendPktCnt = 0;
    m_dataForSend = 0;
    m_pktId = 1234;
}

Dialog::~Dialog()
{
    delete ui;
    if (m_dataForSend != 0)
    {
        delete[] m_dataForSend;
    }
    delete m_udpSocket;
    delete m_ourAddress;
}


void Dialog::on_m_btnOpenEthCard_clicked()
{
    int srcRow = ui->m_listEthCardNames->currentRow();
    if (srcRow <0)
    {
        QMessageBox::critical(this,"Error","Please, select a Source Card");
        return;
    }

    char errbuf[PCAP_ERRBUF_SIZE];


    //QString srcDescription = ui->m_listEthCardNames->item(srcRow,0)->text();
    QTableWidgetItem* srcItem =  ui->m_listEthCardNames->item(srcRow,1);
    m_hCardSource = pcap_open_live(srcItem->text().toLatin1(), 65536, 1, -1, errbuf);
    if (m_hCardSource == 0)
    {
        QMessageBox::critical(this,"Open Source Adapter Error",errbuf);
        return;
    }
    QString tmpAddress = ui->m_listEthCardNames->item(srcRow,2)->text();
    if (tmpAddress.isEmpty()) {
        m_localIp [0] = 192;
        m_localIp [1] = 168;
        m_localIp [2] = 2;
        m_localIp [3] = 6;
    }
    else {
        m_localIp [0] = tmpAddress[0].unicode();
        m_localIp [1] = tmpAddress[2].unicode();
        m_localIp [2] = tmpAddress[4].unicode();
        m_localIp [3] = tmpAddress[6].unicode();
    }

    PPACKET_OID_DATA pOidData;
    CHAR pAddr[512];

    ZeroMemory(pAddr, sizeof(pAddr));
    pOidData = (PPACKET_OID_DATA) pAddr;
    pOidData->Oid = OID_802_3_CURRENT_ADDRESS;
    pOidData->Length = 6;
    LPADAPTER  pADP = PacketOpenAdapter((PCHAR)(srcItem->text().toLatin1().constData()));
    if(PacketRequest(pADP, FALSE, pOidData))
    {
        memcpy(m_macSource, pOidData->Data, 6);
    }

    PacketCloseAdapter(pADP);

    QSettings settings( "YolkaPlay.conf", QSettings::IniFormat );
    settings.beginGroup( "DefaultCards" );
    settings.setValue( "Source", srcItem->text() );
    settings.endGroup();




    ui->m_listEthCardNames->setEnabled(false);
    ui->m_btnCloseEthCard->setEnabled(true);
    ui->m_btnSendPkt->setEnabled(true);
    ui->pushButton->setEnabled(true);
    ui->m_btnOpenEthCard->setEnabled(false);
}

void Dialog::on_m_btnCloseEthCard_clicked()
{
    pcap_close (m_hCardSource);
    m_hCardSource = 0;


    ui->m_btnCloseEthCard->setEnabled(false);
    ui->m_btnSendPkt->setEnabled(false);
    ui->pushButton->setEnabled(false);
    ui->m_btnOpenEthCard->setEnabled(true);
    ui->m_listEthCardNames->setEnabled(true);

}

void Dialog::on_m_btnSendPkt_clicked()
{

    m_arpThread.m_pDialog = this;
    m_arpThread.start(QThread::HighestPriority);

    addrAndPort sourceParams;
    sourceParams.mac = m_macSource;
    sourceParams.ip = inet_addr("10.0.0.2");
    sourceParams.port = 12345;

    static const uint8_t fakeDestMac [6]={0x01,0x02,0x03,0x04,0x05,0x06};

    addrAndPort destParams;
    destParams.mac = (uint8_t*) fakeDestMac;
    destParams.ip = inet_addr("10.0.0.3");
    destParams.port = 12345;

#define jewjq
#ifdef jewjq
/*    for (int i=0;i<3;i++)
    {
        static const uint8_t arppkt[] ={
            0x02,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,0x4c,0x68,0x26,0x18,
            0x08,0x06,0x00,0x01,0x08,0x00,0x06,0x04,0x00,0x01,0x00,0xe0,
            0x4c,0x68,0x26,0x18,0xc0,0xa8,0x02,0x05,0x02,0x00,0x00,0x00,
            0x00,0x00,0xc0,0xa8,0x02,0x80
        };
        udpData arpData;
        arpData.SetUserSize(sizeof(arppkt)-42);
        memcpy (arpData.m_pData,arppkt,sizeof(arppkt));
        pcap_sendpacket(m_hCardSource,arpData.m_pData,arpData.m_totalDataSize);*/

    static const uint8_t pkt[] ={
        0x02,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,
        0x4c,0x68,0x26,0x18,0x08,0x00,0x45,0x00,
        0x00,0x31,0x2a,0x5d,0x00,0x00,0x80,0x11,
        0x00,0x00,0xc0,0xa8,0x02,0x05,0xc0,0xa8,0x02,
        0x80,0xd8,0xdd,0x04,0xd2,0x00,0x1d,0x86,0x04,0x74,0x65,
        0x73,0x74,0x74,0x65,0x73,0x74,0x74,0x65,0x73,0x74,0x74,
        0x65,0x73,0x74,0x74,0x65,0x73,0x74,0x0a
    };
    udpData udpData;
    udpData.SetUserSize(sizeof(pkt)-42);
    memcpy (udpData.m_pData,pkt,sizeof(pkt));
    udpData.m_pData[0x12] = (uint8_t) (m_pktId / 0x100);
    udpData.m_pData[0x13] = (uint8_t) m_pktId;
    m_pktId += 1;

    unsigned short UDPChecksum = CalculateUDPChecksum(udpData);
    memcpy((void*)(udpData.m_pData+40),(void*)&UDPChecksum,2);

    unsigned short IPChecksum = htons(CalculateIPChecksum(udpData/*,TotalLen,0x1337,source.ip,destination.ip*/));
    memcpy((void*)(udpData.m_pData+24),(void*)&IPChecksum,2);

    udpData.SaveToFile("udpPacket.txt");

    pcap_sendpacket(m_hCardSource,udpData.m_pData,udpData.m_totalDataSize);

    Sleep (500);
    if (m_rcvThread.isRunning())
    {
        m_rcvThread.requestInterruption();
    }


//    }
#else
    static const int dataSize = 0x22;
    udpData udpData;
    udpData.SetUserSize(dataSize);
    for (int i=0;i<dataSize;i++)
    {
        udpData.m_pUserData[i] = (uint8_t)(i + ((~i)<<4));
    }
    // Well. Data is filled, now we can add extra information
    CreatePacket(sourceParams,destParams,udpData);
    pcap_sendpacket(m_hCardSource,udpData.m_pData,udpData.m_totalDataSize);
#endif


}
void Dialog::CreatePacket( addrAndPort& source,
                           addrAndPort& destination,
                           udpData& packet)
{
    USHORT TotalLen = packet.m_userSize + 20 + 8; // IP Header uses length of data plus length of ip header (usually 20 bytes) plus lenght of udp header (usually 8)
    //Beginning of Ethernet II Header
    memcpy((void*)packet.m_pData,(void*)destination.mac,6);
    memcpy((void*)(packet.m_pData+6),(void*)source.mac,6);
    USHORT TmpType = 8;
    memcpy((void*)(packet.m_pData+12),(void*)&TmpType,2); //The type of protocol used. (USHORT) Type 0x08 is UDP. You can change this for other protocols (e.g. TCP)
    // Beginning of IP Header
    memcpy((void*)(packet.m_pData+14),(void*)"\x45",1); //The Version (4) in the first 3 bits  and the header length on the last 5. (Im not sure, if someone could correct me plz do)
                                                     //If you wanna do any IPv6 stuff, you will need to change this. but i still don't know how to do ipv6 myself =s
    memcpy((void*)(packet.m_pData+15),(void*)"\x00",1); //Differntiated services field. Usually 0
    TmpType = htons(TotalLen);
    memcpy((void*)(packet.m_pData+16),(void*)&TmpType,2);
    TmpType = htons(0x1337);
    memcpy((void*)(packet.m_pData+18),(void*)&TmpType,2);// Identification. Usually not needed to be anything specific, esp in udp. 2 bytes (Here it is 0x1337
    memcpy((void*)(packet.m_pData+20),(void*)"\x00",1); // Flags. These are not usually used in UDP either, more used in TCP for fragmentation and syn acks i think
    memcpy((void*)(packet.m_pData+21),(void*)"\x00",1); // Offset
    memcpy((void*)(packet.m_pData+22),(void*)"\x80",1); // Time to live. Determines the amount of time the packet can spend trying to get to the other computer. (I see 128 used often for this)
    memcpy((void*)(packet.m_pData+23),(void*)"\x11",1);// Protocol. UDP is 0x11 (17) TCP is 6 ICMP is 1 etc
    memcpy((void*)(packet.m_pData+24),(void*)"\x00\x00",2); //checksum
    memcpy((void*)(packet.m_pData+26),(void*)&source.ip,4); //inet_addr does htonl() for us
    memcpy((void*)(packet.m_pData+30),(void*)&destination.ip,4);
    //Beginning of UDP Header
    TmpType = htons(source.port);
    memcpy((void*)(packet.m_pData+34),(void*)&TmpType,2);
    TmpType = htons(destination.port);
    memcpy((void*)(packet.m_pData+36),(void*)&TmpType,2);
    USHORT UDPTotalLen = htons(packet.m_userSize + 8); // UDP Length does not include length of IP header
    memcpy((void*)(packet.m_pData+38),(void*)&UDPTotalLen,2);
    //memcpy((void*)(FinalPacket+40),(void*)&TmpType,2); //checksum
//Already    memcpy((void*)(packet.m_pData+42),(void*)UserData,UserDataLen);

    unsigned short UDPChecksum = CalculateUDPChecksum(packet);
    memcpy((void*)(packet.m_pData+40),(void*)&UDPChecksum,2);

    unsigned short IPChecksum = htons(CalculateIPChecksum(packet/*,TotalLen,0x1337,source.ip,destination.ip*/));
    memcpy((void*)(packet.m_pData+24),(void*)&IPChecksum,2);

//    memset (packet.m_pData,0,packet.m_totalDataSize);

//    uint32_t crc = CalculateCRC(packet.m_pData,packet.m_totalDataSize);
/*    packet.m_pData[packet.m_totalDataSize-4] = (uint8_t)(crc/0x1);
    packet.m_pData[packet.m_totalDataSize-3] = (uint8_t)(crc/0x100);
    packet.m_pData[packet.m_totalDataSize-2] = (uint8_t)(crc/0x10000);
    packet.m_pData[packet.m_totalDataSize-1] = (uint8_t)(crc/0x1000000);*/

    return;

}

unsigned short Dialog::CalculateUDPChecksum(udpData& packet)
{
    unsigned short CheckSum = 0;
    unsigned short PseudoLength = packet.m_userSize + 8 + 9; //Length of PseudoHeader = Data Length + 8 bytes UDP header (2Bytes Length,2 Bytes Dst Port, 2 Bytes Src Port, 2 Bytes Checksum)
                                                        //+ Two 4 byte IP's + 1 byte protocol
    PseudoLength += PseudoLength % 2; //If bytes are not an even number, add an extra.
    unsigned short Length = packet.m_userSize + 8; // This is just UDP + Data length. needed for actual data in udp header

    unsigned char* PseudoHeader = new unsigned char [PseudoLength];
    for(int i = 0;i < PseudoLength;i++){PseudoHeader[i] = 0x00;}

    PseudoHeader[0] = 0x11;

    memcpy((void*)(PseudoHeader+1),(void*)(packet.m_pData+26),8); // Source and Dest IP

    Length = htons(Length);
    memcpy((void*)(PseudoHeader+9),(void*)&Length,2);
    memcpy((void*)(PseudoHeader+11),(void*)&Length,2);

    memcpy((void*)(PseudoHeader+13),(void*)(packet.m_pData+34),2);
    memcpy((void*)(PseudoHeader+15),(void*)(packet.m_pData+36),2);

    memcpy((void*)(PseudoHeader+17),(void*)packet.m_pUserData,packet.m_userSize);


    for(int i = 0;i < PseudoLength;i+=2)
    {
        unsigned short Tmp = BytesTo16(PseudoHeader[i],PseudoHeader[i+1]);
        unsigned short Difference = 65535 - CheckSum;
        CheckSum += Tmp;
        if(Tmp > Difference){CheckSum += 1;}
    }
    CheckSum = ~CheckSum; //One's complement
    return CheckSum;

}

unsigned short Dialog::CalculateIPChecksum(Dialog::udpData &packet/*, UINT TotalLen, UINT ID, UINT SourceIP, UINT DestIP*/)
{
    unsigned short CheckSum = 0;
    for(int i = 14;i<34;i+=2)
    {
        unsigned short Tmp = BytesTo16(packet.m_pData[i],packet.m_pData[i+1]);
        unsigned short Difference = 65535 - CheckSum;
        CheckSum += Tmp;
        if(Tmp > Difference){CheckSum += 1;}
    }
    CheckSum = ~CheckSum;
    return CheckSum;
}
uint32_t Dialog::CalculateCRC(uint8_t* pData,int size)
{

    int i, j;
    unsigned int byte, crc, mask;

    i = 0;
    crc = 0xFFFFFFFF;
    while (i < size) {
       byte = pData[i];            // Get next byte.
       crc = crc ^ byte;
       for (j = 7; j >= 0; j--) {    // Do eight times.
          mask = -(crc & 1);
          crc = (crc >> 1) ^ (0xEDB88320 & mask);
       }
       i = i + 1;
    }
    return ~crc;
/*
    const uint8_t crcBitsInByte = 8;
    const uint32_t CrcMSBit = 1ul << (sizeof(uint32_t) * crcBitsInByte - 1);
    const uint32_t CrcPoly = 0x04C11DB7;
    uint32_t sum = 0xffffffff;
    uint32_t length = size;
    auto pdata = pData;
    for (uint32_t i = 0; i < length; i++)
    {
        sum ^= pdata[i];
        for (uint8_t bit = 0; bit < crcBitsInByte; bit++)
        {
            if ((sum & CrcMSBit) == CrcMSBit)
                sum = (sum << 1) ^ CrcPoly;
            else
                sum <<= 1;
        }
    }
    return sum;*/
}


void CReceiverThread::run()
{
    while (!isInterruptionRequested())
    {
        pcap_pkthdr *header;
        const u_char * pData;
        int res = pcap_next_ex(m_pDialog->m_hCardSource, &header, &pData);
        if (res == 1)
        {
            if (header->len != header->caplen)
            {
                volatile int stop = 0;
                (void) stop;
            }
            receivedPacket* pPkt = new receivedPacket(header->len);
            m_pDialog->m_receivedPackets.push_back(pPkt);
            pPkt->SetTimeStamp(m_pDialog->m_timer.nsecsElapsed());
            memcpy (pPkt->GetHdrPtr(),header,sizeof(pcap_pkthdr));
            memcpy (pPkt->GetData(),pData,header->caplen);
        }
    }
}
void CARPThread::run()
{
    while (!isInterruptionRequested())
    {
        pcap_pkthdr *header;
        const u_char * pData;
        int res = pcap_next_ex(m_pDialog->m_hCardSource, &header, &pData);
        if (res == 1)
        {
            uint32_t reqType = pData [12] * 0x100 + pData [13];
            uint32_t opCode = pData [20] * 0x100 + pData [21];
            // This is an ARP packet
            if ((reqType == 0x806) && (opCode == 0x0001))
            {
                Dialog::udpData udpData;
                // Header Only
                udpData.SetUserSize(0);
                memset (udpData.m_pData,0,udpData.m_totalDataSize);
                // Copy destinatin MAC address
                memcpy (udpData.m_pData,pData+6,6);
                // Copy Source MAC address
                memcpy (udpData.m_pData+6,m_pDialog->m_macSource,6);

                // This is an ARP Packet
                udpData.m_pData [12] = 0x08;
                udpData.m_pData [13] = 0x06;

                // Hardware Type - Ethernet
                udpData.m_pData [14] = 0x00;
                udpData.m_pData [15] = 0x01;

                // Protocol is IPv4
                udpData.m_pData [16] = 0x08;
                udpData.m_pData [17] = 0x00;

                // Hardware Size
                udpData.m_pData [18] = 0x06;

                // Protocol Size
                udpData.m_pData [19] = 0x04;

                // This is an Reply!
                udpData.m_pData [20] = 0x00;
                udpData.m_pData [21] = 0x02;

                memcpy (udpData.m_pData+22,m_pDialog->m_macSource,6);
                memcpy (udpData.m_pData+28,m_pDialog->m_localIp,4);
                memcpy (udpData.m_pData+32,pData+22,6);
                memcpy (udpData.m_pData+38,pData+28,4);

                udpData.SaveToFile("arpAnswer.txt");

                pcap_sendpacket(m_pDialog->m_hCardSource,udpData.m_pData,udpData.m_totalDataSize);

            }

            receivedPacket* pPkt = new receivedPacket(header->len);
            m_pDialog->m_receivedPackets.push_back(pPkt);
            pPkt->SetTimeStamp(m_pDialog->m_timer.nsecsElapsed());
            memcpy (pPkt->GetHdrPtr(),header,sizeof(pcap_pkthdr));
            memcpy (pPkt->GetData(),pData,header->caplen);
        }
    }
}

void Dialog::on_pushButton_clicked()
{
    // Clear results of previous
    for (auto i = m_receivedPackets.begin();i!=m_receivedPackets.end();++i)
    {
//        (*i)->clear();
        delete (*i);
    }
    m_receivedPackets.clear();

    m_rcvThread.m_pDialog = this;
    m_rcvThread.start(QThread::HighestPriority);


    // These parameters will be same for all packets
    for (size_t i=0;i<sizeof(m_macSource);i++)
    {
        m_macDestination[i] = ~m_macSource[i];
    }

    addrAndPort sourceParams;
    sourceParams.mac = m_macSource;
    sourceParams.ip = inet_addr("192.168.2.5");
    sourceParams.port = 1234;

    addrAndPort destParams;
    destParams.mac = m_macDestination;
    destParams.ip = inet_addr("192.168.2.128");
    destParams.port = 1234;


    // Create Packets for send
    m_nSendPktCnt =ui->m_loopsCnt->text().toUInt();
    if ((m_nSendPktCnt <= 0) || (m_nSendPktCnt>1000000))
    {
        QMessageBox::critical(this,"Error","Wrong Loops Counter");
        return;
    }
    if (m_dataForSend != 0)
    {
        delete[] m_dataForSend;
    }
    m_dataForSend = new udpData [m_nSendPktCnt];

    srand (GetTickCount()/*1234*/);

    qint64 totalSize = 0;
    // Create all packets for send.
    for (int i=0;i<m_nSendPktCnt;i++)
    {
        int len;
        do
        {
            len = rand()%1024;
            len = 1024;
        } while ((len<=18)/*||(len%4!=2)*/);

        totalSize += len;
        m_dataForSend[i].SetUserSize(len);

        // Fill Random data
        for (int j=m_dataForSend[i].GetUserSize()-1;j>=0;j--)
        {
            m_dataForSend[i].m_pUserData[j] = (uint8_t)rand();
//            m_dataForSend[i].m_pUserData[j] = j;
        }
        CreatePacket(sourceParams,destParams,m_dataForSend[i]);
    }

    bool bSleepBetween = ui->m_cbSleepBetween->isChecked();
    int delta = 10;
    if (bSleepBetween)
    {
        delta = 220;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // OK. Now we are ready to send data...
    m_timer.start();

//#define SINGLE_MODE
#ifdef SINGLE_MODE
    for (int i=0;i<m_nSendPktCnt;i++)
    {
            pcap_sendpacket(m_hCardSource,m_dataForSend[i].m_pData,m_dataForSend[i].m_totalDataSize);
            if (bSleepBetween)
            {
                Sleep (1);
//                QThread::msleep(1);
            }
    }
#else
    for (int i=0;i<m_nSendPktCnt;i+=100)
    {
        pcap_send_queue *squeue = pcap_sendqueue_alloc (10 * 1024 * 1024);

        pcap_pkthdr hdr;
        memset (&hdr,0,sizeof(hdr));

        for (int j=0;((j<100) && (i+j < m_nSendPktCnt));j++)
        {
            hdr.caplen = m_dataForSend[i+j].m_totalDataSize;
            hdr.len = m_dataForSend[i+j].m_totalDataSize;
            hdr.ts.tv_usec += delta;
            if (hdr.ts.tv_usec > 1000000)
            {
                hdr.ts.tv_usec %= 1000000;
                hdr.ts.tv_sec += 1;
            }
            pcap_sendqueue_queue (squeue,&hdr,m_dataForSend[i+j].m_pData);
        }
        pcap_sendqueue_transmit(m_hCardSource, squeue, 1);
        /* free the send queue */
        pcap_sendqueue_destroy(squeue);
    }
#endif


    qint64 testTime =  m_timer.nsecsElapsed();
    // For finish receive process
    Sleep (1000);
    QApplication::restoreOverrideCursor();


    if (m_rcvThread.isRunning())
    {
        m_rcvThread.requestInterruption();
    }
    while (m_rcvThread.isRunning())
    {
        QThread::msleep(10);
    }

    int nGood = 0;
    int totallyReceived = m_receivedPackets.size();
    for (int i=0;i<m_nSendPktCnt;i++)
    {
        // When we are testing MAC layer, we are inversing data for
        // be sure that this is really processed information
        // But in UDP layer it requires deep pipelining that is why
        // we will use original data
        int dataOffset = 0;
        if (ui->m_cbInverse->isChecked())
        {
            for (int j=0;j<m_dataForSend[i].m_totalDataSize;j++)
            {
                m_dataForSend[i].m_pData[j] = ~m_dataForSend[i].m_pData[j];
            }
        } else
        {
            dataOffset = 42;
            for (int j=0;j<m_dataForSend[i].m_totalDataSize;j++)
            {
                m_dataForSend[i].m_pData[j] = m_dataForSend[i].m_pData[j]+1;
            }
        }
        for (auto j = m_receivedPackets.begin();j!=m_receivedPackets.end();++j)
        {
            receivedPacket* rcvPkt = *j;
            if (m_dataForSend[i].m_totalDataSize == rcvPkt->GetSize())
            {
                if (memcmp(m_dataForSend[i].m_pData+dataOffset,rcvPkt->GetData()+dataOffset,rcvPkt->GetSize()-dataOffset)==0)
                {
                   nGood += 1;
                   delete rcvPkt;
                   m_receivedPackets.erase(j);
                   break;
                }
            }
        }
    }
    double speed = (double)totalSize/(double)testTime;
    speed *= 1000000000.;
    QString infoLine = QString::number(totalSize) + " bytes totally\n";
    infoLine += QString ("%1'%2'%3 us\n").arg(testTime/1000000000).arg((testTime/1000000)%1000,3,10,QChar('0')).arg((testTime/1000)%1000,3,10,QChar('0'));
    infoLine += QString::number((int)speed) + " bytes per seconds\n";
    infoLine += QString("%1 packets totally, %2 of %3 are correct").arg(totallyReceived).arg(nGood).arg(m_nSendPktCnt);
    QMessageBox::information(this,"Info",infoLine);
}

void Dialog::on_m_btnExportBad_clicked()
{
    if (m_receivedPackets.size()==0)
    {
        QMessageBox::information(this,"Nothing to do","There are no any packets for export");
        return;
    }
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Target Directory for many files"),
                                                 "",
                                                 QFileDialog::ShowDirsOnly
                                                 | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty())
    {
        return;
    }
    QByteArray ar;
    int n = 0;
    for (auto j = m_receivedPackets.begin();j!=m_receivedPackets.end();++j)
    {
        receivedPacket* rcvPkt = *j;

        if (memcmp (rcvPkt->GetData(),m_macSource,6)!=0)
        {
            continue;
        }

        QFile file (dir + "/" + QString::number(n++) + ".bin");
        file.open(QIODevice::WriteOnly);

        // We need invert data
        if (ar.size()<rcvPkt->GetSize())
        {
            ar.resize(rcvPkt->GetSize()*2);
        }
        uint8_t* pData= (uint8_t*)ar.constData();
        memcpy (pData,rcvPkt->GetData(),rcvPkt->GetSize());
        for (int k = 0;k<rcvPkt->GetSize();k++)
        {
            pData [k] = ~pData[k];
        }
        file.write((char*)pData,rcvPkt->GetSize());
        file.close();
    }
}

void Dialog::on_m_btnCrewateTestPkt_clicked()
{
/*    uint8_t dataC [64];
    memset (dataC,0x12,sizeof(dataC));
    uint32_t crc = CalculateCRC(dataC,32);


    dataC[32] = ~(uint8_t)(crc/0x1);
    dataC[33] = ~(uint8_t)(crc/0x100);
    dataC[34] = ~(uint8_t)(crc/0x10000);
    dataC [35] = ~(uint8_t)(crc/0x1000000);
    uint32_t crc2 = CalculateCRC(dataC,36);

    int stop = 0;*/


    udpData data;
    data.SetUserSize(64);
    for (int i=0;i<data.GetUserSize();i++)
    {
        data.m_pUserData[i] = (uint8_t)i;
    }
    addrAndPort sourceParams;
    sourceParams.mac = m_macSource;
    sourceParams.ip = inet_addr("10.0.0.2");
    sourceParams.port = 12345;

    static const uint8_t fakeDestMac [6]={0x01,0x02,0x03,0x04,0x05,0x06};

    addrAndPort destParams;
    destParams.mac = (uint8_t*) fakeDestMac;
    destParams.ip = inet_addr("10.0.0.3");
    destParams.port = 12345;

    static const int dataSize = 0x22;
    udpData udpData;
    udpData.SetUserSize(dataSize);
    for (int i=0;i<dataSize;i++)
    {
        udpData.m_pUserData[i] = (uint8_t)(i + ((~i)<<4));
    }

    // Well. Data is filled, now we can add extra information
    CreatePacket(sourceParams,destParams,data);

    QFile file ("file1.txt");
    file.open(QIODevice::WriteOnly);

    for (int i=0;i<data.m_totalDataSize;i++)
    {
        char txt [16];
        sprintf (txt,"%02X\r\n",data.m_pData[i]);
        file.write(txt,strlen(txt));
    }

    file.close();

}

bool Dialog::ReadEeprom(int addr, int size, QByteArray &data)
{
    static const uint8_t pkt[] ={
        0x02,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,    // +0
        0x4c,0x68,0x26,0x18,0x08,0x00,0x45,0x00,    // +8
        0x00,0x31,0x2a,0x5d,0x00,0x00,0x80,0x11,    // + 0x10
        0x00,0x00,0xc0,0xa8,0x02,0x05,0xc0,0xa8,    // + 0x18
        0x02,0x80,0xd8,0xdd,0xEE,0xE0,0x00,0x1d,    // + 0x20
        0x86,0x04,0x74,0x65,0x73,0x74,0x74,0x65,    // + 0x28
        0x73,0x74,0x74,0x65,0x73,0x74,0x74,0x65,    // + 0x30
        0x73,0x74,0x74,0x65,0x73,0x74,0x0a
    };

    udpData udpData;
    udpData.SetUserSize(sizeof(pkt)-42);
    memcpy (udpData.m_pData,pkt,sizeof(pkt));
    memcpy (udpData.m_pData+0x1a,m_localIp,4);

    int offset = 0;
    data.resize(size);
    while (size > 0)
    {
        *((DWORD*)(udpData.m_pData+0x2a)) = addr;
        m_pktId += 1;
        udpData.m_pData[0x12] = (uint8_t) (m_pktId / 0x100);
        udpData.m_pData[0x13] = (uint8_t) m_pktId;

        unsigned short UDPChecksum = CalculateUDPChecksum(udpData);
        memcpy((void*)(udpData.m_pData+40),(void*)&UDPChecksum,2);

        unsigned short IPChecksum = htons(CalculateIPChecksum(udpData/*,TotalLen,0x1337,source.ip,destination.ip*/));
        memcpy((void*)(udpData.m_pData+24),(void*)&IPChecksum,2);

//        udpData.SaveToFile("ReadEepromWrongMac.txt");

        pcap_sendpacket(m_hCardSource,udpData.m_pData,udpData.m_totalDataSize);

        QTimer timeOut;
        timeOut.start (1000);
        // Todo Catch timeout
        while (true)
        {
            if (timeOut.remainingTime()==0)
            {
                return false;
            }
            pcap_pkthdr *header;
            const u_char * pData;
            int res = pcap_next_ex(m_hCardSource, &header, &pData);
            if (res == 1)
            {
                // Check Destination IP TODO

                // Check Destination Port
                int destPort = *((uint16_t*)(pData+0x24));
                if (destPort!=0xe1ee)
                {
                    continue;
                }
                int lenForCpy = (int)(header->len-0x2a);
                if (size < lenForCpy)
                {
                    lenForCpy = size;
                }
                memcpy (((void*)(data.constData()+offset)),(const char*)(pData+0x2a),lenForCpy);
                break;
            }
        }
        addr += 0x80;
        offset += 0x80;
        size -= 0x80;
    }
}
bool Dialog::WriteEeprom(int addr, int size, const char* data)
{
    static const uint8_t pkt[] ={
        0x02,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,    // +0
        0x4c,0x68,0x26,0x18,0x08,0x00,0x45,0x00,    // +8
        0x00,0x31,0x2a,0x5d,0x00,0x00,0x80,0x11,    // + 0x10
        0x00,0x00,0xc0,0xa8,0x02,0x05,0xc0,0xa8,    // + 0x18
        0x02,0x80,0xd8,0xdd,0xEE,0xE2,0x00,0x8B,    // + 0x20
        0x86,0x04,0x74,0x65,0x73,0x74,0x74,0x65,    // + 0x28
        0x73,0x74,0x74,0x65,0x73,0x74,0x74,0x65,    // + 0x30
        0x73,0x74,0x74,0x65,0x73,0x74,0x0a
    };

    udpData udpData;
    udpData.SetUserSize(0x83);
    memcpy (udpData.m_pData,pkt,sizeof(pkt));
    memcpy (udpData.m_pData+0x1a,m_localIp,4);


    int offset = 0;
    while (size > 0)
    {
        m_pktId += 1;
        udpData.m_pData[0x12] = (uint8_t) (m_pktId / 0x100);
        udpData.m_pData[0x13] = (uint8_t) m_pktId;
        int lenForCopy = 0x80;
        if (size < lenForCopy)
        {
            lenForCopy = size;
        }
        *((DWORD*)(udpData.m_pData+0x2a)) = addr;
        memcpy (udpData.m_pUserData+3,data+offset,lenForCopy);
        udpData.m_pData [0x27] = lenForCopy+3+8;
        int totalIpLenght = lenForCopy + 28 + 3;
        udpData.m_pData [0x10] = (uint8_t)(totalIpLenght / 0x100);
        udpData.m_pData [0x11] = (uint8_t)(totalIpLenght / 0x1);

        unsigned short UDPChecksum = CalculateUDPChecksum(udpData);
        memcpy((void*)(udpData.m_pData+40),(void*)&UDPChecksum,2);

        unsigned short IPChecksum = htons(CalculateIPChecksum(udpData/*,TotalLen,0x1337,source.ip,destination.ip*/));
        memcpy((void*)(udpData.m_pData+24),(void*)&IPChecksum,2);

        //    udpData.SaveToFile("udpPacket.txt");
        m_udpSocket->bind(*m_ourAddress, 1234);
        QByteArray data((char*)(udpData.m_pData), udpData.m_userSize);
        m_udpSocket->writeDatagram(data, *m_ourAddress, 1234);
        //pcap_sendpacket(m_hCardSource,udpData.m_pData,totalIpLenght+0x0e/*udpData.m_totalDataSize*/);

        addr += 0x80;
        offset += 0x80;
        size -= 0x80;
        Sleep (10);
    }
}


void Dialog::on_m_btnReadEeprom_2_clicked()
{
    int addr = ui->m_editEepromAddr->text().toULong(nullptr,0);
    int size = ui->m_editEepromSize->text().toULong(nullptr,0);
    QByteArray ar;
    if (ReadEeprom(addr,size,ar))
    {
        ui->m_eepromDump->setData(ar);
    } else
    {
        QMessageBox::critical(this,"Error","Cannot Read Eeprom");
    }
}

void Dialog::on_m_btnWriteEeprom_clicked()
{
    int addr = ui->m_editEepromAddr->text().toULong(nullptr,0);
    int size = ui->m_editEepromSize->text().toULong(nullptr,0);
    QByteArray ar = ui->m_eepromDump->data();
    const char* data = ar.constData();
    if (!WriteEeprom(addr,size,data))
    {
        QMessageBox::critical(this,"Error","Cannot Write Eeprom");
    }
}

void Dialog::on_m_btnEraseEeprom_clicked()
{
    static const uint8_t pkt[] ={
        0x02,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,    // +0
        0x4c,0x68,0x26,0x18,0x08,0x00,0x45,0x00,    // +8
        0x00,0x31,0x2a,0x5d,0x00,0x00,0x80,0x11,    // + 0x10
        0x00,0x00,0xc0,0xa8,0x02,0x05,0xc0,0xa8,    // + 0x18
        0x02,0x80,0xd8,0xdd,0xEE,0xEE,0x00,0x1d,    // + 0x20       // Port 0xeeee
        0x86,0x04,0x74,0x65,0x73,0x74,0x74,0x65,    // + 0x28
        0x73,0x74,0x74,0x65,0x73,0x74,0x74,0x65,    // + 0x30
        0x73,0x74,0x74,0x65,0x73,0x74,0x0a
    };

    int addr = ui->m_editEepromAddr->text().toULong(nullptr,0);

    udpData udpData;
    udpData.SetUserSize(sizeof(pkt)-42);
    memcpy (udpData.m_pData,pkt,sizeof(pkt));
    memcpy (udpData.m_pData+0x1a,m_localIp,4);
    udpData.m_pData[0x12] = (uint8_t) (m_pktId / 0x100);
    udpData.m_pData[0x13] = (uint8_t) m_pktId;

    *((DWORD*)(udpData.m_pData+0x2a)) = addr;
    m_pktId += 1;

    unsigned short UDPChecksum = CalculateUDPChecksum(udpData);
    memcpy((void*)(udpData.m_pData+40),(void*)&UDPChecksum,2);

    unsigned short IPChecksum = htons(CalculateIPChecksum(udpData/*,TotalLen,0x1337,source.ip,destination.ip*/));
    memcpy((void*)(udpData.m_pData+24),(void*)&IPChecksum,2);

    udpData.SaveToFile("udpEraseEeprom.txt");

    pcap_sendpacket(m_hCardSource,udpData.m_pData,udpData.m_totalDataSize);

}

void Dialog::on_m_btnCreateDhcpTemplete_clicked()
{
    static const uint8_t tmpData[] = {
        2,0,0,0,0,0,
        192,168,2,128,
        192,168,2,1,
        255,255,255,0
        };
    QByteArray templateDhcpPkt;
    templateDhcpPkt.setRawData(reinterpret_cast<const char*>(tmpData), sizeof (tmpData));
    ui->m_eepromDump->setData(templateDhcpPkt);
    ui->m_editEepromSize->setText(QString("0x%1").arg(sizeof(tmpData), 3, 16,QChar('0')));
}


void Dialog::on_m_btnSendDHCPOffer_clicked()
{
    static const uint8_t pkt[] ={
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5c, 0xf4, 0xab, 0xcf, 0xa4, 0x30, 0x08, 0x00, 0x45, 0x00,
        0x01, 0x48, 0x67, 0xac, 0x40, 0x00, 0x40, 0x11, 0x4e, 0x75, 0xc0, 0xa8, 0x01, 0x01, 0xc0, 0xa8,
        0x01, 0x32, 0x00, 0x43, 0x00, 0x44, 0x01, 0x34, 0xbc, 0xc5, 0x02, 0x01, 0x06, 0x00, 0xaa, 0x1f,
        0x00, 0x5e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xa8, 0x01, 0x32, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x4c, 0x68, 0x26, 0x18, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x82, 0x53, 0x63, 0x35, 0x01, 0x02, 0x36, 0x04, 0xc0,
        0xa8, 0x01, 0x01, 0x33, 0x04, 0x00, 0x00, 0x62, 0x70, 0x3a, 0x04, 0x00, 0x00, 0x31, 0x38, 0x3b,
        0x04, 0x00, 0x00, 0x56, 0x22, 0x01, 0x04, 0xff, 0xff, 0xff, 0x00, 0x03, 0x04, 0xc0, 0xa8, 0x01,
        0x01, 0x06, 0x04, 0xc0, 0xa8, 0x01, 0x01, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    int addr = ui->m_editEepromAddr->text().toULong(nullptr,0);

    udpData udpData;
    udpData.SetUserSize(sizeof(pkt)-42);
    memcpy (udpData.m_pData,pkt,sizeof(pkt));

    unsigned short UDPChecksum = CalculateUDPChecksum(udpData);
    memcpy((void*)(udpData.m_pData+40),(void*)&UDPChecksum,2);

    unsigned short IPChecksum = htons(CalculateIPChecksum(udpData/*,TotalLen,0x1337,source.ip,destination.ip*/));
    memcpy((void*)(udpData.m_pData+24),(void*)&IPChecksum,2);

//    udpData.SaveToFile("DHCPoffer.txt");

    pcap_sendpacket(m_hCardSource,udpData.m_pData,udpData.m_totalDataSize);

}

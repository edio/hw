/*
 * Hedgewars, a free turn based strategy game
 * Copyright (c) 2006-2007 Igor Ulyanov <iulyanov@gmail.com>
 * Copyright (c) 2004-2012 Andrey Korotaev <unC0Rr@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include "tcpBase.h"

#include <QMessageBox>
#include <QList>
#include <QApplication>
#include <QImage>

#include "hwconsts.h"

QList<TCPBase*> srvsList;
QPointer<QTcpServer> TCPBase::IPCServer(0);

TCPBase::~TCPBase()
{
    // make sure this object is not in the server list anymore
    srvsList.removeOne(this);

    if (IPCSocket)
        IPCSocket->deleteLater();
}

TCPBase::TCPBase(bool demoMode) :
    m_hasStarted(false),
    m_isDemoMode(demoMode),
    IPCSocket(0)
{
    if(!IPCServer)
    {
        IPCServer = new QTcpServer(0);
        IPCServer->setMaxPendingConnections(1);
        if (!IPCServer->listen(QHostAddress::LocalHost))
        {
            QMessageBox deniedMsg(QApplication::activeWindow());
            deniedMsg.setIcon(QMessageBox::Critical);
            deniedMsg.setWindowTitle(QMessageBox::tr("TCP - Error"));
            deniedMsg.setText(QMessageBox::tr("Unable to start the server: %1.").arg(IPCServer->errorString()));
            deniedMsg.setWindowModality(Qt::WindowModal);
            deniedMsg.exec();

            exit(0); // FIXME - should be graceful exit here (lower Critical -> Warning above when implemented)
        }
    }
    ipc_port=IPCServer->serverPort();
}

void TCPBase::NewConnection()
{
    if(IPCSocket)
    {
        // connection should be already finished
        return;
    }
    disconnect(IPCServer, SIGNAL(newConnection()), this, SLOT(NewConnection()));
    IPCSocket = IPCServer->nextPendingConnection();
    if(!IPCSocket) return;
    connect(IPCSocket, SIGNAL(disconnected()), this, SLOT(ClientDisconnect()));
    connect(IPCSocket, SIGNAL(readyRead()), this, SLOT(ClientRead()));
    SendToClientFirst();
}

void TCPBase::RealStart()
{
    connect(IPCServer, SIGNAL(newConnection()), this, SLOT(NewConnection()));
    IPCSocket = 0;

    QProcess * process;
    process = new QProcess();
    connect(process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(StartProcessError(QProcess::ProcessError)));
    QStringList arguments=getArguments();

    // redirect everything written on stdout/stderr
    if(isDevBuild)
        process->setProcessChannelMode(QProcess::ForwardedChannels);

    process->start(bindir->absolutePath() + "/hwengine", arguments);

    m_hasStarted = true;
}

void TCPBase::ClientDisconnect()
{
    disconnect(IPCSocket, SIGNAL(readyRead()), this, SLOT(ClientRead()));
    onClientDisconnect();

    emit isReadyNow();
    IPCSocket->deleteLater();

    deleteLater();
}

void TCPBase::ClientRead()
{
    QByteArray readed=IPCSocket->readAll();
    if(readed.isEmpty()) return;
    readbuffer.append(readed);
    onClientRead();
}

void TCPBase::StartProcessError(QProcess::ProcessError error)
{
    QMessageBox deniedMsg(QApplication::activeWindow());
    deniedMsg.setIcon(QMessageBox::Critical);
    deniedMsg.setWindowTitle(QMessageBox::tr("TCP - Error"));
    deniedMsg.setText(QMessageBox::tr("Unable to run engine at ") + bindir->absolutePath() + "/hwengine\n" +
                      QMessageBox::tr("Error code: %1").arg(error));
    deniedMsg.setWindowModality(Qt::WindowModal);
    deniedMsg.exec();

    ClientDisconnect();
}

void TCPBase::tcpServerReady()
{
    disconnect(srvsList.first(), SIGNAL(isReadyNow()), this, SLOT(tcpServerReady()));

    RealStart();
}

void TCPBase::Start(bool couldCancelPreviousRequest)
{
    if(srvsList.isEmpty())
    {
        srvsList.push_back(this);
        RealStart();
    }
    else
    {
        if(couldCancelPreviousRequest && srvsList.last()->couldBeRemoved())
        {
            TCPBase * last = srvsList.takeLast();
            last->deleteLater();
            Start(couldCancelPreviousRequest);
        } else
        {
            connect(srvsList.last(), SIGNAL(isReadyNow()), this, SLOT(tcpServerReady()));
            srvsList.push_back(this);
        }
    }
}

void TCPBase::onClientRead()
{
}

void TCPBase::onClientDisconnect()
{
}

void TCPBase::SendToClientFirst()
{
}

void TCPBase::SendIPC(const QByteArray & buf)
{
    if (buf.size() > MAXMSGCHARS) return;
    quint8 len = buf.size();
    RawSendIPC(QByteArray::fromRawData((char *)&len, 1) + buf);
}

void TCPBase::RawSendIPC(const QByteArray & buf)
{
    if (!IPCSocket)
    {
        toSendBuf += buf;
    }
    else
    {
        if (toSendBuf.size() > 0)
        {
            IPCSocket->write(toSendBuf);
            if(m_isDemoMode) demo.append(toSendBuf);
            toSendBuf.clear();
        }
        if(!buf.isEmpty())
        {
            IPCSocket->write(buf);
            if(m_isDemoMode) demo.append(buf);
        }
    }
}

bool TCPBase::couldBeRemoved()
{
    return false;
}

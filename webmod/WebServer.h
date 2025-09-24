#pragma once

class CWebServer
{
public:
    void ServerActivate();
    void ServerDeactivate();
    void StartFrame();
    static void ServerHandle(tw_conn *Connection, tw_request *Request, tw_response *Response);

    tw_server m_Server;
    bool m_Running;
};

extern CWebServer gWebServer;

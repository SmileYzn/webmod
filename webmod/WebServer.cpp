#define THINWIRE_IMPL

#include "precompiled.h"

CWebServer gWebServer;

void CWebServer::ServerActivate()
{
    const char* NetAddress = g_engfuncs.pfnCVarGetString("net_address");

    if (NetAddress)
    {
        if (NetAddress[0u] != '\0')
        {
            if (!this->m_Running)
            {
                this->m_Running = tw_server_init(&this->m_Server, 27020);

                if (this->m_Running)
                {
                    tw__set_nonblocking(1);
                }
            }
        }
    }
}

void CWebServer::ServerDeactivate()
{
    if (this->m_Running)
    {
        tw_server_stop(&this->m_Server);
    }
}

void CWebServer::StartFrame()
{
    if (this->m_Running)
    {
        tw_server_run(&this->m_Server, this->ServerHandle);
    }
}

void CWebServer::ServerHandle(tw_conn *Connection, tw_request *Request, tw_response *Response)
{
  const char *message = "Ola Mundo, estamos travando seu HLDS!";

  tw_response_set_status(Response, 200);
  tw_response_set_header(Response, "Content-Type", "text/plain");

  tw_response_set_body(Response, message, strlen(message));

  tw_response_send(Connection, Response);
}
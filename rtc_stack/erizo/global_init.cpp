#include "global_init.h"

#include <glib.h>
#include <nice/debug.h>

#include "erizo/dtls/DtlsSocket.h"
#include "erizo/LibNiceConnection.h"

typedef void (*GLogFunc) ( \
  const gchar *log_domain,  GLogLevelFlags log_level, const gchar *message, gpointer user_data);

GLogFunc g_libnice_log_;

#define WA_G_LOG_DOMAIN   "wa"

namespace erizo
{

int erizo_global_init()
{
  dtls::DtlsSocketContext::Init();

  g_libnice_log_ = (GLogFunc)LibNiceConnection::libnice_log;

  g_log_set_handler(WA_G_LOG_DOMAIN, 
      G_LOG_LEVEL_DEBUG,
      g_libnice_log_,
      nullptr);

  nice_debug_enable(1);
  
  return 0;
}

int erizo_global_release() 
{
  dtls::DtlsSocketContext::Destroy();
  return 0;
}

}


//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "media_file_handler.h"

#include "myrtc/api/default_task_queue_factory.h"
#include "common/media_log.h"
#include "http/h/http_message.h"
#include "http/h/http_protocal.h"
#include "utils/media_protocol_utility.h"
#include "media_error_handler.h"
#include "media_server.h"

namespace ma {

#define SRS_HTTP_DEFAULT_PAGE "index.html"

std::string srs_http_fs_fullpath(
      std::string dir, std::string pattern, std::string upath) {
    // add default pages.
  if (srs_string_ends_with(upath, "/")) {
      upath += SRS_HTTP_DEFAULT_PAGE;
  }

  // Remove the virtual directory.
  // For example:
  //      pattern=/api, the virtual directory is api, upath=/api/index.html, fullpath={dir}/index.html
  //      pattern=/api, the virtual directory is api, upath=/api/views/index.html, fullpath={dir}/views/index.html
  // The vhost prefix is ignored, for example:
  //      pattern=ossrs.net/api, the vhost is ossrs.net, the pattern equals to /api under this vhost,
  //      so the virtual directory is also api
  size_t pos = pattern.find("/");
  std::string filename = upath;
  if (upath.length() > pattern.length() && pos != std::string::npos) {
      filename = upath.substr(pattern.length() - pos);
  }

  std::string fullpath = srs_string_trim_end(dir, "/");
  if (!srs_string_starts_with(filename, "/")) {
      fullpath += "/";
  }
  fullpath += filename;

  return fullpath;
}

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.fileserver");

srs_error_t MediaFileHandler::serve_http(
    std::shared_ptr<IHttpResponseWriter> w, 
    std::shared_ptr<ISrsHttpMessage> r) {

  static HttpNotFoundHandler s_not_found;
  std::string pattern{""};
  // use short-term HTTP connection.
  SrsHttpHeader* h = w->header();
  h->set("Connection", "Close");

  std::string upath = r->path();
  std::string fullpath = 
      srs_http_fs_fullpath(g_server_.config_.path, "/test", upath);

  // stat current dir, if exists, return error.
  if (!srs_path_exists(fullpath)) {
    MLOG_CWARN("http miss file=%s, pattern=%s, upath=%s",
             fullpath.c_str(), pattern.c_str(), upath.c_str());
    return s_not_found.serve_http(std::move(w), std::move(r));
  }
  MLOG_CTRACE("http match file=%s, pattern=%s, upath=%s",
            fullpath.c_str(), pattern.c_str(), upath.c_str());

  if (!worker_) {
    auto task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
    worker_ = std::make_shared<wa::Worker>(task_queue_factory.get());
    worker_->start("file server");
  }

  // serve common static file.
  worker_->task([this, w, r, fullpath] {
      serve_file(std::move(w), std::move(r), fullpath);
    }, RTC_FROM_HERE);
  
  return srs_success;
}

static std::map<std::string, std::string> g_mime {
  {".ts", "video/MP2T"},
  {".flv", "video/x-flv"},
  {".m4v", "video/x-m4v"},
  {".3gpp", "video/3gpp"},
  {".3gp", "video/3gpp"},
  {".mp4", "video/mp4"},
  {".aac", "audio/x-aac"},
  {".mp3", "audio/mpeg"},
  {".m4a", "audio/x-m4a"},
  {".ogg", "audio/ogg"},
  // @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 5.
  {".m3u8", "application/vnd.apple.mpegurl"}, // application/x-mpegURL
  {".rss", "application/rss+xml"},
  {".json", "application/json"},
  {".swf", "application/x-shockwave-flash"},
  {".doc", "application/msword"},
  {".zip", "application/zip"},
  {".rar", "application/x-rar-compressed"},
  {".xml", "text/xml"},
  {".html", "text/html"},
  {".js", "text/javascript"},
  {".css", "text/css"},
  {".ico", "image/x-icon"},
  {".png", "image/png"},
  {".jpeg", "image/jpeg"},
  {".jpg", "image/jpeg"},
  {".gif", "image/gif"},
  {".mpd", "text/xml"},
  {".m4s", "video/iso.segment"},
  {".mp4v", "video/mp4"}
};

srs_error_t MediaFileHandler::serve_file(
    std::shared_ptr<IHttpResponseWriter> w, 
    std::shared_ptr<ISrsHttpMessage> r, 
    const std::string&  fullpath) {
  srs_error_t err = srs_success;

  std::unique_ptr<SrsFileReader> fs( 
      ISrsFileReaderFactory().create_file_reader());

  if ((err = fs->open(fullpath)) != srs_success) {
    return srs_error_wrap(err, "open file %s", fullpath.c_str());
  }

  // The length of bytes we could response to.
  int64_t length = fs->filesize() - fs->tellg();
  
  // unset the content length to encode in chunked encoding.
  w->header()->set_content_length(length);
  
  std::string ext = srs_path_filext(fullpath);
  
  if (g_mime.find(ext) == g_mime.end()) {
      w->header()->set_content_type("application/octet-stream");
  } else {
      w->header()->set_content_type(ext);
  }

  // Enter chunked mode, because we didn't set the content-length.
  w->write_header(SRS_CONSTS_HTTP_OK);
  
  // write body.
  int64_t left = length;
  if ((err = copy(std::move(w), std::move(fs), std::move(r), (int)left)) != srs_success) {
    return srs_error_wrap(err, "copy file=%s size=%d", fullpath.c_str(), (int)left);
  }
    
  return err;
}
    
void MediaFileHandler::conn_destroy(std::shared_ptr<IMediaConnection> c) {
  std::lock_guard<std::mutex> guard(lock_);
  s_list_.erase(c.get());
}

static constexpr int MEDIA_HTTP_SEND_BUFFER_SIZE = MA_MAX_PACKET_SIZE;

srs_error_t MediaFileHandler::copy(
    std::shared_ptr<IHttpResponseWriter> w, 
    std::unique_ptr<SrsFileReader> fs, 
    std::shared_ptr<ISrsHttpMessage> msg, 
    int size) {
  srs_error_t err = srs_success;
  
  int file_left = size;

  std::unique_ptr<char[]> buf(new char[MEDIA_HTTP_SEND_BUFFER_SIZE]);
  while (file_left > 0) {
    ssize_t nread = -1;
    int max_read = std::min(file_left, MEDIA_HTTP_SEND_BUFFER_SIZE);
    if ((err = fs->read(buf.get(), max_read, &nread)) != srs_success) {
      return srs_error_wrap(err, "read limit=%d, left=%d", max_read, file_left);
    }

    file_left -= nread;

    if ((err = w->write(buf.get(), (int)nread)) != srs_success) {
      if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
        return srs_error_wrap(err, 
            "error on underlying socket, write limit=%d, bytes=%d, left=%d", 
            max_read, (int)nread, file_left);
      }

      // would block, save job
      auto job = std::make_shared<doing_job>();
      w->SignalOnWrite_.connect(job.get(), &doing_job::on_write_event);
      job->fs_ = std::move(fs);
      job->buf_ = std::move(buf);
      job->w_ = std::move(w);
      job->buf_left_ = nread;
      job->file_left_ = file_left;
      job->worker_ = worker_;
      {
        std::lock_guard<std::mutex> guard(lock_);
        s_list_.emplace(msg->connection().get(), std::move(job));
      }
      delete err;
      return srs_success;
    }
  }

  if ((err = w->final_request()) != srs_success) {
    return srs_error_wrap(err, "final request");
  }
  return err;
}

void MediaFileHandler::doing_job::on_write_event(IHttpResponseWriter* w) {
  auto func = [this, w]() {
    MA_ASSERT(w == w_.get());
    srs_error_t err = srs_success;
    if (buf_left_ <= 0) {
      return;
    }

    // send buffer first
    if ((err = w->write(buf_.get(), buf_left_)) != srs_success) {
      if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
        MLOG_CERROR("error on underlying socket, left=%d, desc:%s", 
            buf_left_ + file_left_, srs_error_desc(err).c_str());
        delete err;
        return ;
      }

      // unreachable code
      MA_ASSERT(false);
    }

    buf_left_ = 0;
    
    while (file_left_ > 0) {
      ssize_t nread = -1;
      int max_read = std::min(file_left_, MEDIA_HTTP_SEND_BUFFER_SIZE);
      if ((err = fs_->read(buf_.get(), max_read, &nread)) != srs_success) {
        MLOG_CERROR("read limit=%d, left=%d, desc:%s", 
            max_read, file_left_, srs_error_desc(err).c_str());
        delete err;
        return ;
      }

      MA_ASSERT(max_read == nread && nread > 0);
      file_left_ -= nread;

      if ((err = w->write(buf_.get(), (int)nread)) != srs_success) {
        if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
          MLOG_CERROR("error on underlying socket, left=%d, desc:%s", 
            nread + file_left_, srs_error_desc(err).c_str());
          delete err;
          return ;
        }

        buf_left_ = nread;
        delete err;
        return ;
      }
    }

    if ((err = w->final_request()) != srs_success) {
      MLOG_CERROR("final request, desc:%s", srs_error_desc(err).c_str());
      delete err;
    }
  };

  auto weak_this = weak_from_this();
  worker_->task([weak_this, func] {
      if (auto this_ptr = weak_this.lock()) {
        func();
      }
    }, RTC_FROM_HERE);
}

}


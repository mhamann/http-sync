/* -*- indent-tabs-mode: nil; c-basic-offset: 2; tab-width: 2 -*- */

/* This code is PUBLIC DOMAIN, and is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND. See the accompanying
 * LICENSE file.
 */

#include <nan.h>
#include <curl/curl.h>
#include <string>
#include <string.h>
#include <vector>
#include <stdint.h>
#include <iostream>

using namespace node;
using namespace v8;

#define THROW_BAD_ARGS \
  Nan::ThrowTypeError("Bad argument")

#define PERSISTENT_STRING(id, text) \
  id.Reset(Nan::New<String>(text).ToLocalChecked())

typedef std::vector<char> buff_t;

class CurlLib : ObjectWrap {
private:
  static std::string buffer;
  static std::vector<std::string> headers;
  static Nan::Persistent<String> sym_body_length;
  static Nan::Persistent<String> sym_headers;
  static Nan::Persistent<String> sym_timedout;
  static Nan::Persistent<String> sym_error;

public:
  static Nan::Persistent<Function> s_constructor;
  static void Init(Handle<Object> target) {
    Local<FunctionTemplate> t = Nan::New<FunctionTemplate>(New);

    t->InstanceTemplate()->SetInternalFieldCount(1);
    t->SetClassName(Nan::New<String>("CurlLib").ToLocalChecked());

    Nan::SetPrototypeMethod(t, "run", Run);
    SetPrototypeMethod(t, "body", Body);

    s_constructor.Reset(t->GetFunction());
    target->Set(Nan::New<String>("CurlLib").ToLocalChecked(),
                t->GetFunction());

    PERSISTENT_STRING(sym_body_length, "body_length");
    PERSISTENT_STRING(sym_headers, "headers");
    PERSISTENT_STRING(sym_timedout, "timedout");
    PERSISTENT_STRING(sym_error, "error");
  }

  CurlLib() { }
  ~CurlLib() { }

  static NAN_METHOD(New) {
    CurlLib* curllib = new CurlLib();
    curllib->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  }

  static size_t write_data(void *ptr, size_t size,
			   size_t nmemb, void *userdata) {
    buffer.append(static_cast<char*>(ptr), size * nmemb);
    // std::cerr<<"Wrote: "<<size*nmemb<<" bytes"<<std::endl;
    // std::cerr<<"Buffer size: "<<buffer.size()<<" bytes"<<std::endl;
    return size * nmemb;
  }

  static size_t write_headers(void *ptr, size_t size, size_t nmemb, void *userdata)
  {
    std::string header(static_cast<char*>(ptr), size * nmemb);
    headers.push_back(header);
    return size * nmemb;
  }

  static NAN_METHOD(Body) {
    if (info.Length() < 1 || !Buffer::HasInstance(info[0])) {
      return THROW_BAD_ARGS;
    }

    Local<Object> buffer_obj = info[0]->ToObject();
    char *buffer_data        = Buffer::Data(buffer_obj);
    size_t buffer_length     = Buffer::Length(buffer_obj);

    if (buffer_length < buffer.size()) {
      return Nan::ThrowTypeError("Insufficient Buffer Length");
    }

    if (!buffer.empty()) {
      memcpy(buffer_data, buffer.data(), buffer.size());
    }
    buffer.clear();
    info.GetReturnValue().Set(buffer_obj);
  }

  static NAN_METHOD(Run) {
    if (info.Length() < 1) {
      return THROW_BAD_ARGS;
    }

    Local<String> key_method = Nan::New<String>("method").ToLocalChecked();
    Local<String> key_url = Nan::New<String>("url").ToLocalChecked();
    Local<String> key_headers = Nan::New<String>("headers").ToLocalChecked();
    Local<String> key_body = Nan::New<String>("body").ToLocalChecked();
    Local<String> key_connect_timeout_ms = Nan::New<String>("connect_timeout_ms").ToLocalChecked();
    Local<String> key_timeout_ms = Nan::New<String>("timeout_ms").ToLocalChecked();
    Local<String> key_rejectUnauthorized = Nan::New<String>("rejectUnauthorized").ToLocalChecked();
    Local<String> key_caCert = Nan::New<String>("ca").ToLocalChecked();
    Local<String> key_clientCert = Nan::New<String>("cert").ToLocalChecked();
    Local<String> key_pfx = Nan::New<String>("pfx").ToLocalChecked();
    Local<String> key_clientKey = Nan::New<String>("key").ToLocalChecked();
    Local<String> key_clientKeyPhrase = Nan::New<String>("passphrase").ToLocalChecked();

    static const Local<String> PFXFORMAT = Nan::New<String>("P12").ToLocalChecked();

    Local<Array> opt = Local<Array>::Cast(info[0]);

    if (!opt->Has(key_method) ||
        !opt->Has(key_url) ||
        !opt->Has(key_headers)) {
      return THROW_BAD_ARGS;
    }

    if (!opt->Get(key_method)->IsString() ||
        !opt->Get(key_url)->IsString()) {
      return THROW_BAD_ARGS;
    }

    Local<String> method = Local<String>::Cast(opt->Get(key_method));
    Local<String> url    = Local<String>::Cast(opt->Get(key_url));
    Local<Array>  reqh   = Local<Array>::Cast(opt->Get(key_headers));
    Local<String> body   = Nan::New<String>((const char*)"", 0).ToLocalChecked();
    Local<String> caCert   = Nan::New<String>((const char*)"", 0).ToLocalChecked();
    Local<String> clientCert   = Nan::New<String>((const char*)"", 0).ToLocalChecked();
    Local<String> clientCertFormat   = Nan::New<String>((const char*)"", 0).ToLocalChecked();
    Local<String> clientKey   = Nan::New<String>((const char*)"", 0).ToLocalChecked();
    Local<String> clientKeyPhrase   = Nan::New<String>((const char*)"", 0).ToLocalChecked();
    long connect_timeout_ms = 1 * 60 * 60 * 1000; /* 1 hr in msec */
    long timeout_ms = 1 * 60 * 60 * 1000; /* 1 hr in msec */
    bool rejectUnauthorized = false;

    if (opt->Has(key_caCert) && opt->Get(key_caCert)->IsString()) {
      caCert = opt->Get(key_caCert)->ToString();
    }

    if (opt->Has(key_clientKey) && opt->Get(key_clientKey)->IsString()) {
      clientKey = opt->Get(key_clientKey)->ToString();
    }

    if (opt->Has(key_clientKeyPhrase) && opt->Get(key_clientKeyPhrase)->IsString()) {
      clientKeyPhrase = opt->Get(key_clientKeyPhrase)->ToString();
    }

    if (opt->Has(key_clientCert) && opt->Get(key_clientCert)->IsString()) {
      clientCert = opt->Get(key_clientCert)->ToString();
    } else if (opt->Has(key_pfx) && opt->Get(key_pfx)->IsString()) {
      clientCert = opt->Get(key_pfx)->ToString();
      clientCertFormat = PFXFORMAT;
    }

    if (opt->Has(key_body) && opt->Get(key_body)->IsString()) {
      body = opt->Get(key_body)->ToString();
    }

    if (opt->Has(key_connect_timeout_ms) && opt->Get(key_connect_timeout_ms)->IsNumber()) {
      connect_timeout_ms = opt->Get(key_connect_timeout_ms)->IntegerValue();
    }

    if (opt->Has(key_timeout_ms) && opt->Get(key_timeout_ms)->IsNumber()) {
      timeout_ms = opt->Get(key_timeout_ms)->IntegerValue();
    }

    if (opt->Has(key_rejectUnauthorized)) {
      // std::cerr<<"has reject unauth"<<std::endl;
      if (opt->Get(key_rejectUnauthorized)->IsBoolean()) {
        rejectUnauthorized = opt->Get(key_rejectUnauthorized)->BooleanValue();
      } else if (opt->Get(key_rejectUnauthorized)->IsBooleanObject()) {
        rejectUnauthorized = opt->Get(key_rejectUnauthorized)
          ->ToBoolean()
          ->BooleanValue();
      }
    }

    // std::cerr<<"rejectUnauthorized: " << rejectUnauthorized << std::endl;

    Nan::Utf8String _body(body);
    Nan::Utf8String _method(method);
    Nan::Utf8String _url(url);
    Nan::Utf8String _cacert(caCert);
    Nan::Utf8String _clientcert(clientCert);
    Nan::Utf8String _clientcertformat(clientCertFormat);
    Nan::Utf8String _clientkeyphrase(clientKeyPhrase);
    Nan::Utf8String _clientkey(clientKey);

    std::vector<std::string> _reqh;
    for (size_t i = 0; i < reqh->Length(); ++i) {
      _reqh.push_back(*Nan::Utf8String(reqh->Get(i)));
    }

    // CurlLib* curllib = ObjectWrap::Unwrap<CurlLib>(info.This());

    buffer.clear();
    headers.clear();

    CURL *curl;
    CURLcode res = CURLE_FAILED_INIT;

    // char error_buffer[CURL_ERROR_SIZE];
    // error_buffer[0] = '\0';

    curl = curl_easy_init();
    if (curl) {
      // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
      // curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, *_method);
      if (_body.length() > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, *_body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                         (curl_off_t)_body.length());
      }
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
      curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5);
      curl_easy_setopt(curl, CURLOPT_URL, *_url);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
      curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_headers);

      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);

      if (rejectUnauthorized) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
      } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
      }

      if (_cacert.length() > 0) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, *_cacert);
      }

      if (_clientcert.length() > 0) {
        if (_clientcertformat.length() > 0) {
          curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, *_clientcertformat);
        }
        curl_easy_setopt(curl, CURLOPT_SSLCERT, *_clientcert);
      }

      if (_clientkeyphrase.length() > 0) {
        curl_easy_setopt(curl, CURLOPT_KEYPASSWD, *_clientkeyphrase);
      }

      if (_clientkey.length() > 0) {
        curl_easy_setopt(curl, CURLOPT_SSLKEY, *_clientkey);
      }

      struct curl_slist *slist = NULL;

      for (size_t i = 0; i < _reqh.size(); ++i) {
        slist = curl_slist_append(slist, _reqh[i].c_str());
      }

      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);

      res = curl_easy_perform(curl);

      curl_slist_free_all(slist);

      /* always cleanup */
      curl_easy_cleanup(curl);
    }

    // std::cerr<<"error_buffer: "<<error_buffer<<std::endl;

    Local<Object> result = Nan::New<Object>();

    if (!res) {
      result->Set(Nan::New(sym_body_length), Nan::New<Integer>((int32_t)buffer.size()));
      Local<Array> _h = Nan::New<Array>();
      for (size_t i = 0; i < headers.size(); ++i) {
        _h->Set(i, Nan::New<String>(headers[i].c_str()).ToLocalChecked());
      }
      result->Set(Nan::New(sym_headers), _h);
    }
    else if (res == CURLE_OPERATION_TIMEDOUT) {
      result->Set(Nan::New(sym_timedout), Nan::New<Integer>(1));
    } else {
      result->Set(Nan::New(sym_error), Nan::New<String>(curl_easy_strerror(res)).ToLocalChecked());
    }

    headers.clear();
    info.GetReturnValue().Set(result);
  }
};

Nan::Persistent<Function> CurlLib::s_constructor;
std::string CurlLib::buffer;
std::vector<std::string> CurlLib::headers;
Nan::Persistent<String> CurlLib::sym_body_length;
Nan::Persistent<String> CurlLib::sym_headers;
Nan::Persistent<String> CurlLib::sym_timedout;
Nan::Persistent<String> CurlLib::sym_error;

extern "C" {

static void init(Handle<Object> target) {
  CurlLib::Init(target);
}
#ifndef LUMIN
NODE_MODULE(NODE_GYP_MODULE_NAME, init)
#else
extern "C" {
  void node_register_module_window_http_sync(Local<Object> exports, Local<Value> module, Local<Context> context) {
    init(exports);
  }
}
#endif
  
}


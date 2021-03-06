//
// Copyright (C) 2013-2014 Vanamco AG
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//

#ifndef __extension_handler__
#define __extension_handler__


#include "lib\Libcef\Include/cef_process_message.h"
#include "lib\Libcef\Include/cef_v8.h"

#include "types.h"
#include "client_app.h"
#include "client_handler.h"
#include "native_extensions.h"


#if !defined(OS_WIN) // NO_ERROR is defined on windows
static const int NO_ERROR                   = 0;
#endif

static const int ERR_UNKNOWN                = 1;
static const int ERR_INVALID_PARAM_NUM      = 2;
static const int ERR_INVALID_PARAM_TYPES    = 3;


#define END_MARKER -999


#define INVOKE_CALLBACK TEXT("@invokeCallback")
#define CALLBACK_COMPLETED TEXT("@callbackCompleted")
#define THROW_EXCEPTION TEXT("@throwException")


typedef int (*Function)(
    CefRefPtr<ClientHandler> handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<ExtensionState> ext,
    CefRefPtr<CefListValue> args,
    CefRefPtr<CefListValue> ret
);

typedef void (*CallbacksCompleteHandler)(
    CefRefPtr<ClientHandler> handler,
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<ExtensionState> ext
);


class ClientCallback
{
public:
    ClientCallback(int32 messageId, CefRefPtr<CefBrowser> browser)
        : m_messageId(messageId), m_browser(browser), m_invokeJavaScriptCallbackCount(0)
    {
    }
    
    ~ClientCallback();
    
    void Invoke(String functionName, CefRefPtr<CefListValue> args);
    
    inline int32 GetMessageId()
    {
        return m_messageId;
    }
    
    inline int IncrementJavaScriptInvokeCallbackCount()
    {
        return ++m_invokeJavaScriptCallbackCount;
    }
    
    inline int GetJavaScriptInvokeCallbackCount()
    {
        return m_invokeJavaScriptCallbackCount;
    }
    
private:
    int32 m_messageId;
    CefRefPtr<CefBrowser> m_browser;
    int m_invokeJavaScriptCallbackCount;
};


class NativeFunction
{
public:
    NativeFunction(Function fnx, ...);
    ~NativeFunction();
    
    int Call(
        CefRefPtr<ClientHandler> handler, CefRefPtr<CefBrowser> browser, CefRefPtr<ExtensionState> state,
        CefRefPtr<CefListValue> args, CefRefPtr<CefListValue> ret);
    void AddCallback(int messageId, CefBrowser* browser);
    String GetArgList();
    
    int GetNumArgs()
    {
        return (int) m_argNames.size();
    }
    
    void SetAllCallbacksCompletedHandler(CallbacksCompleteHandler fnxAllCallbacksCompleted)
    {
        m_fnxAllCallbacksCompleted = fnxAllCallbacksCompleted;
    }

private:
    // A function pointer to the native implementation
    Function m_fnx;
    
    std::vector<int> m_argTypes;
    std::vector<String> m_argNames;
    
public:
    String m_name;
    bool m_hasPersistentCallback;
    std::vector<ClientCallback*> m_callbacks;

    // Function to invoke when all JavaScript callbacks have completed
    CallbacksCompleteHandler m_fnxAllCallbacksCompleted;
};


class NativeJavaScriptFunctionAdder
{
public:
    inline void AddNativeJavaScriptProcedure(String name, NativeFunction* fnx, String customJavaScriptImplementation = TEXT(""))
    {
        AddNativeJavaScriptFunction(name, fnx, false, false, customJavaScriptImplementation);
    }
    
    inline void AddNativeJavaScriptCallback(String name, NativeFunction* fnx, String customJavaScriptImplementation = TEXT(""))
    {
        AddNativeJavaScriptFunction(name, fnx, false, true, customJavaScriptImplementation);
    }
    
    virtual void AddNativeJavaScriptFunction(String name, NativeFunction* fnx, bool hasReturnValue = true, bool hasPersistentCallback = false, String customJavaScriptImplementation = TEXT("")) = 0;

protected:
	String CreateArgList(NativeFunction* fnx, bool hasReturnValue, bool hasPersistentCallback)
	{
		String argList = fnx->GetArgList();
		if (hasReturnValue || hasPersistentCallback)
		{
			if (fnx->GetNumArgs() > 0)
				argList.append(TEXT(","));
			argList.append(TEXT("callback"));
		}

		return argList;
	}
};


class ClientExtensionHandler : public NativeJavaScriptFunctionAdder, public ClientHandler::ProcessMessageDelegate
{
public:
    ClientExtensionHandler();
    ~ClientExtensionHandler();
    
    virtual void ReleaseCefObjects();
    
	virtual void AddNativeJavaScriptFunction(String name, NativeFunction* fnx, bool hasReturnValue = true, bool hasPersistentCallback = false, String customJavaScriptImplementation = TEXT(""));

	bool InvokeCallbacks(String functionName, CefRefPtr<CefListValue> args);
    
    inline CefRefPtr<ExtensionState> GetState()
    {
        return m_state;
    }
    
    
    // ProcessMessageDelegate Implementation
    
    virtual bool OnProcessMessageReceived(CefRefPtr<ClientHandler> handler, CefRefPtr<CefBrowser> browser,
        CefProcessId source_process, CefRefPtr<CefProcessMessage> message);
    
private:
    std::map<String, NativeFunction*> m_mapFunctions;
    CefRefPtr<ExtensionState> m_state;
    
    IMPLEMENT_REFCOUNTING(ClientExtensionHandler);
};


class AppCallback
{
public:
    AppCallback(CefRefPtr<CefV8Context> context, CefRefPtr<CefV8Value> function)
        : m_context(context), m_function(function)
    {
    }

	~AppCallback()
	{
		m_context = NULL;
		m_function = NULL;
	}
    
    CefRefPtr<CefV8Context> GetContext()
    {
        return m_context;
    }
    
    CefRefPtr<CefV8Value> GetFunction()
    {
        return m_function;
    }
    
private:
    CefRefPtr<CefV8Context> m_context;
    CefRefPtr<CefV8Value> m_function;
};


// Handles the native implementation for the JavaScript app extension.
class AppExtensionHandler : public NativeJavaScriptFunctionAdder, public CefV8Handler, public ClientApp::RenderDelegate
{
public:
    AppExtensionHandler();
	~AppExtensionHandler();

    // NativeJavaScriptFunctionAdder Implementation
	virtual void AddNativeJavaScriptFunction(String name, NativeFunction* fnx, bool hasReturnValue = true, bool hasPersistentCallback = false, String customJavaScriptImplementation = TEXT(""));
	String GetJavaScriptCode();

    // CefV8Handler Implementation
    virtual bool Execute(const CefString& name, CefRefPtr<CefV8Value> object, const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& retval, CefString& exception);

    // RenderDelegate Implementation
    virtual void OnBrowserCreated(CefRefPtr<ClientApp> app, CefRefPtr<CefBrowser> browser);
    virtual void OnContextReleased(CefRefPtr<ClientApp> app, CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context);
    virtual bool OnProcessMessageReceived(CefRefPtr<ClientApp> app, CefRefPtr<CefBrowser> browser, CefProcessId source_process, CefRefPtr<CefProcessMessage> message);
    
private:
    void AddCallback(CefRefPtr<CefV8Value> fnx);
    bool HasPersistentCallback(String functionName);
    void ThrowJavaScriptException(CefRefPtr<CefV8Context> context, CefString functionName, int retval);
    
private:
    String m_JavaScriptCode;
    std::map<String, bool> m_mapFunctionHasPersistentCallback;

    // map of message callbacks
    std::map<int32, AppCallback*> m_mapCallbacks;
    
    int32 m_messageId;
        
    IMPLEMENT_REFCOUNTING(AppExtensionHandler);
};


#endif /* defined(__extension_handler__) */

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

#include <stdint.h>

#include "app.h"
#include "extension_handler.h"
#include "util.h"
#include "v8_util.h"
#include "jsbridge.h"


#ifdef OS_WIN
#include <tchar.h>
#endif


///////////////////////////////////////////////////////////////
// ClientCallback Implementation

ClientCallback::~ClientCallback()
{
    m_browser = NULL;
}

void ClientCallback::Invoke(String functionName, CefRefPtr<CefListValue> args)
{
    CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create(INVOKE_CALLBACK);
    CefRefPtr<CefListValue> responseArgs = response->GetArgumentList();
        
    responseArgs->SetInt(0, m_messageId);
    responseArgs->SetString(1, functionName);
    responseArgs->SetInt(2, NO_ERROR);
    CopyList(args, responseArgs, 3);
        
    // send to the renderer process; this will be handled by AppExtensionHandler::OnProcessMessageReceived
    if (m_browser != NULL)
        m_browser->SendProcessMessage(PID_RENDERER, response);
}


///////////////////////////////////////////////////////////////
// NativeFunction Implementation

NativeFunction::NativeFunction(Function fnx, ...)
    : m_fnxAllCallbacksCompleted(NULL)
{
    m_fnx = fnx;
    
    va_list vl;
    va_start(vl, fnx);
    for (int i = 0; ; i += 2)
    {
        int nType = va_arg(vl, int);
        if (nType == END_MARKER)
            break;
        
        m_argTypes.push_back(nType);
        m_argNames.push_back(va_arg(vl, TCHAR*));
    }
    va_end(vl);
}

NativeFunction::~NativeFunction()
{
    for (ClientCallback* pCallback : m_callbacks)
        delete pCallback;
}

//
// Calls the native function.
//
int NativeFunction::Call(CefRefPtr<ClientHandler> handler, CefRefPtr<CefBrowser> browser, CefRefPtr<ExtensionState> state, CefRefPtr<CefListValue> args, CefRefPtr<CefListValue> ret)
{
#ifndef NDEBUG
    App::Log(m_name);
#endif

    // check the number of arguments (first argument is the messageId)
    if (args->GetSize() != m_argTypes.size() + 1)
        return ERR_INVALID_PARAM_NUM;
    
    // check the argument types
    for (size_t i = 0; i < m_argTypes.size(); ++i)
        if (m_argTypes.at(i) != VTYPE_INVALID && !JavaScript::HasType(args->GetType((int) i + 1), m_argTypes.at(i)))
            return ERR_INVALID_PARAM_TYPES;
    
    CefRefPtr<CefListValue> fnArgs = CefListValue::Create();
    CopyList(args, fnArgs, -1);
    return m_fnx(handler, browser, state, fnArgs, ret);
}

void NativeFunction::AddCallback(int messageId, CefBrowser* browser)
{
    m_callbacks.push_back(new ClientCallback(messageId, browser));
}

//
// Returns a comma-separated string of argument names.
//
String NativeFunction::GetArgList()
{
    String argList;
    
    bool first = true;
    for (String arg : m_argNames)
    {
        if (!first)
            argList.append(TEXT(", "));
        argList.append(arg);
        
        first = false;
    }
    
    return argList;
}


///////////////////////////////////////////////////////////////
// ClientExtensionHandler Implementation

ClientExtensionHandler::ClientExtensionHandler()
    : m_state(new ExtensionState)
{
    m_state->SetClientExtensionHandler(this);
}

ClientExtensionHandler::~ClientExtensionHandler()
{
}

void ClientExtensionHandler::ReleaseCefObjects()
{
    for (std::map<String, NativeFunction*>::iterator it = m_mapFunctions.begin(); it != m_mapFunctions.end(); ++it)
        delete it->second;
    m_mapFunctions.clear();
	m_state = NULL;
}

//
// Add a native function callable from JavaScript.
//
void ClientExtensionHandler::AddNativeJavaScriptFunction(String name, NativeFunction* fnx, bool hasReturnValue, bool hasPersistentCallback, String customJavaScriptImplementation)
{
    String argList = fnx->GetArgList();
    if (hasReturnValue || hasPersistentCallback)
    {
        if (fnx->GetNumArgs() > 0)
            argList.append(TEXT(","));
        argList.append(TEXT("callback"));
    }
        
    fnx->m_name = name;
    fnx->m_hasPersistentCallback = hasPersistentCallback;
    
    m_mapFunctions[name] = fnx;
}

//
// Invokes the registred callback functions of the function named functionName
// with arguments args.
//
bool ClientExtensionHandler::InvokeCallbacks(String functionName, CefRefPtr<CefListValue> args)
{
    // try to find the function object for the message name
    std::map<String, NativeFunction*>::iterator it = m_mapFunctions.find(functionName);
    if (it == m_mapFunctions.end())
        return false;
    
    // invoke the callbacks
    NativeFunction* fnx = it->second;
    bool isCallbackCalled = false;
    for (ClientCallback* pCallback : fnx->m_callbacks)
    {
        pCallback->Invoke(functionName, args);
        isCallbackCalled = true;
    }
    
    return isCallbackCalled;
}

//
// Browser process.
// Message from the render process received to execute a function.
//
bool ClientExtensionHandler::OnProcessMessageReceived(CefRefPtr<ClientHandler> handler, CefRefPtr<CefBrowser> browser, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
    String name = message->GetName();
    
	if (name == CALLBACK_COMPLETED)
    {
        // the browser process is notified that a JavaScript callback function has completed
        // (only sent for functions with persistent callbacks)
        
        // arguments:
        // 0: message id
        // 1: function name
        
        CefRefPtr<CefListValue> args = message->GetArgumentList();
        int32 messageId = args->GetInt(0);
        
        // get the native function; nothing to do if there is none
        std::map<String, NativeFunction*>::iterator it = m_mapFunctions.find(args->GetString(1));
        if (it == m_mapFunctions.end())
            return true;
        
        NativeFunction* fnx = it->second;
        int invokeCount = -1;
        for (ClientCallback* pCallback : fnx->m_callbacks)
            if (pCallback->GetMessageId() == messageId)
            {
                invokeCount = pCallback->IncrementJavaScriptInvokeCallbackCount();
                break;
            }
        
        // test if all invocations have completed
        if (invokeCount != -1 && fnx->m_fnxAllCallbacksCompleted != NULL)
        {
            bool allCompleted = true;
            for (ClientCallback* pCallback : fnx->m_callbacks)
            {
                if (pCallback->GetJavaScriptInvokeCallbackCount() < invokeCount)
                {
                    allCompleted = false;
                    break;
                }
            }
            
            if (allCompleted)
                fnx->m_fnxAllCallbacksCompleted(handler, browser, m_state);
        }
    }
    else
    {
        // try to find the function object for the message name
        std::map<String, NativeFunction*>::iterator it = m_mapFunctions.find(message->GetName());
        if (it == m_mapFunctions.end())
            return false;
    
        // invoke the native function
        NativeFunction* fnx = it->second;
        CefRefPtr<CefListValue> returnValues = CefListValue::Create();
        int ret = fnx->Call(handler, browser, m_state, message->GetArgumentList(), returnValues);

        // callback handling
        int messageId = message->GetArgumentList()->GetInt(0);
        if (fnx->m_hasPersistentCallback)
        {
            if (ret == NO_ERROR)
            {
                // this function has a persistent callback
                // we don't invoke this callback immediately, but save it so it can be called later
                fnx->AddCallback(messageId, browser);
            }
            else
            {
                // throw and exception
                CefRefPtr<CefProcessMessage> throwExceptionMsg = CefProcessMessage::Create(THROW_EXCEPTION);
                CefRefPtr<CefListValue> args = throwExceptionMsg->GetArgumentList();
                args->SetInt(0, messageId);
                args->SetString(1, message->GetName());
                args->SetInt(2, ret);
                browser->SendProcessMessage(PID_RENDERER, throwExceptionMsg);
            }
        }
        else
        {
            // this function doesn't have a persistent callback;
            // call it immediately to send the response
                
            // a INVOKE_CALLBACK message is sent to the renderer process; the expected arguments are
            // 0: messageId
            // 1: function name
            // 2: return value of the native function
            // 3...: parameters to the callback function
            
            CefRefPtr<CefProcessMessage> responseMsg = CefProcessMessage::Create(INVOKE_CALLBACK);
            CefRefPtr<CefListValue> responseArgs = responseMsg->GetArgumentList();

            responseArgs->SetInt(0, messageId);
            responseArgs->SetString(1, message->GetName());
            responseArgs->SetInt(2, ret);
            CopyList(returnValues, responseArgs, 3);
        
            // send to the renderer process; this will be handled by AppExtensionHandler::OnProcessMessageReceived
            browser->SendProcessMessage(PID_RENDERER, responseMsg);
        }
    }
    
    return true;
}


///////////////////////////////////////////////////////////////
// AppExtensionHandler Implementation

AppExtensionHandler::AppExtensionHandler()
  : m_messageId(0)
{
}

AppExtensionHandler::~AppExtensionHandler()
{
	for (std::map<int32, AppCallback*>::iterator it = m_mapCallbacks.begin(); it != m_mapCallbacks.end(); ++it)
		delete it->second;
	m_mapCallbacks.clear();
}

void AppExtensionHandler::AddNativeJavaScriptFunction(String name, NativeFunction* fnx, bool hasReturnValue, bool hasPersistentCallback, String customJavaScriptImplementation)
{
    // create the JavaScript extension code
    
    // generate this JavaScript code:
    // app.<fnx> = function(<args>, callback) {
    //     native function <fnx>();
    //     return <fnx>(<args>, callback);
    // }

    String argList = CreateArgList(fnx, hasReturnValue, hasPersistentCallback);

    m_JavaScriptCode.append(TEXT("app."));
    m_JavaScriptCode.append(name);
    m_JavaScriptCode.append(TEXT("=function("));
    m_JavaScriptCode.append(argList);
    m_JavaScriptCode.append(TEXT("){\n"));
    m_JavaScriptCode.append(TEXT("  native function "));
    m_JavaScriptCode.append(name);
    m_JavaScriptCode.append(TEXT("();\n"));

    if (customJavaScriptImplementation.length() == 0)
    {
        // call the native function
        m_JavaScriptCode.append(TEXT("  return "));
        m_JavaScriptCode.append(name);
        m_JavaScriptCode.append(TEXT("("));
        m_JavaScriptCode.append(argList);
        m_JavaScriptCode.append(TEXT(");"));
    }
    else
        m_JavaScriptCode.append(customJavaScriptImplementation);

    m_JavaScriptCode.append(TEXT("\n};\n"));

	if (hasPersistentCallback)
		m_mapFunctionHasPersistentCallback[name] = true;
}

//
// Returns the JavaScript code for the V8 extension. The native functions will be available in the global
// "app" object.
//
String AppExtensionHandler::GetJavaScriptCode()
{
    // create the final JavaScript code and try to send it to the render process for registration
    return TEXT("var app; if(!app) app={};\n") + m_JavaScriptCode;
}

//
// Render process.
// JS function invocation calls this function.
//
// If there is a callback function in the last argument, memorize it.
// Send a message to the browser process with the function name and the function arguments.
// The first argument of the message is the ID of the callback function, the subsequent arguments
// are the parameters to the native function
//
bool AppExtensionHandler::Execute(const CefString& name, CefRefPtr<CefV8Value> object, const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& retval, CefString& exception)
{
    CefRefPtr<CefBrowser> browser = CefV8Context::GetCurrentContext()->GetBrowser();
    if (!browser.get())
    {
        // if we don't have a browser, we can't handle the command
        return false;
    }
    
    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(name);
    CefRefPtr<CefListValue> messageArgs = message->GetArgumentList();
    
    // memorize the callback function (callbacks must be the last argument)
    size_t numArgs = arguments.size();
    if (arguments.size() > 0 && arguments[arguments.size() - 1]->IsFunction())
    {
        AddCallback(arguments[arguments.size() - 1]);
        numArgs--;
    }
    else
        AddCallback(NULL);

    // set the first argument: the message id
    messageArgs->SetInt(0, m_messageId);
    
    // Pass the rest of the arguments
    for (size_t i = 0; i < numArgs; i++)
        SetListValue(messageArgs, (int) i + 1, arguments[i]);
    
    // send to the browser process; this will be handled by ClientExtensionHandler::OnProcessMessageReceived
    browser->SendProcessMessage(PID_BROWSER, message);
    
    m_messageId++;
    if (m_messageId > INT32_MAX - 1)
        m_messageId = 0;
    
    return true;
}

void AppExtensionHandler::AddCallback(CefRefPtr<CefV8Value> fnx)
{
    m_mapCallbacks[m_messageId] = new AppCallback(CefV8Context::GetCurrentContext(), fnx);
}

bool AppExtensionHandler::HasPersistentCallback(String functionName)
{
    std::map<String, bool>::iterator it = m_mapFunctionHasPersistentCallback.find(functionName);
    if (it == m_mapFunctionHasPersistentCallback.end())
        return false;
    return it->second;
}

void AppExtensionHandler::OnBrowserCreated(CefRefPtr<ClientApp> app, CefRefPtr<CefBrowser> browser)
{
}

void AppExtensionHandler::OnContextReleased(CefRefPtr<ClientApp> app, CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context)
{
    // Remove any JavaScript callbacks registered for the context that has been released.
    if (!m_mapCallbacks.empty())
    {
        for (std::map<int32, AppCallback*>::iterator it = m_mapCallbacks.begin(); it != m_mapCallbacks.end(); ++it)
            if (it->second->GetContext()->IsSame(context))
			{
				delete it->second;
                m_mapCallbacks.erase(it);
			}
    }
}

//
// Render process.
// Invoke a callback to return a value to a JavaScript function.
//
bool AppExtensionHandler::OnProcessMessageReceived(CefRefPtr<ClientApp> app, CefRefPtr<CefBrowser> browser, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
    ASSERT(source_process == PID_BROWSER);
    
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    String name = message->GetName();
    
    if (name == INVOKE_CALLBACK)
    {
        // invoke a callback function
        
        // arguments:
        // 0: messageId
        // 1: function name
        // 2: return value of the native function
        // 3...: parameters to the callback function
        
        int32 messageId = args->GetInt(0);
        CefString functionName = args->GetString(1);
        
#ifndef NDEBUG
        App::Log(TEXT("Invoking callback ") + String(functionName));
#endif
        
        std::map<int32, AppCallback*>::iterator it = m_mapCallbacks.find(messageId);
        
        if (it != m_mapCallbacks.end())
        {
            AppCallback* callback = it->second;

            CefRefPtr<CefV8Context> context = callback->GetContext();

            // sanity check to make sure the context is still attched to a browser.
            // Async callbacks could be initiated after a browser instance has been deleted,
            // which can lead to bad things. If the browser instance has been deleted, don't
            // invoke this callback.
            if (context->GetBrowser())
            {
                context->Enter();
                
                int retval = args->GetInt(2);
                if (retval == NO_ERROR)
                {
                    CefRefPtr<CefV8Value> function = callback->GetFunction();
                    if (function.get())
                    {
                        // prepare the arguments for the callback
                        CefV8ValueList arguments;
                        for (size_t i = 3; i < args->GetSize(); i++)
                            arguments.push_back(ListValueToV8Value(args, (int) i));
                
                        // execute the callback function
                        function->ExecuteFunctionWithContext(context, NULL, arguments);
                        
                        // send a message that the callback has been completed
                        if (HasPersistentCallback(functionName))
                        {
                            CefRefPtr<CefProcessMessage> cbCompletedMsg = CefProcessMessage::Create(CALLBACK_COMPLETED);
                            CefRefPtr<CefListValue> cbCompletedArgs = cbCompletedMsg->GetArgumentList();
                            cbCompletedArgs->SetInt(0, messageId);
                            cbCompletedArgs->SetString(1, functionName);
                            browser->SendProcessMessage(PID_BROWSER, cbCompletedMsg);
                        }
                    }
                }
                else
                    ThrowJavaScriptException(context, functionName, retval);
                
                context->Exit();
            }

            // remove the callback if it isn't set to be persistent
            if (!HasPersistentCallback(functionName))
			{
				delete it->second;
                m_mapCallbacks.erase(it);
			}
        }
        
        return true;
    }
    else if (name == THROW_EXCEPTION)
    {
        // throw an exception
        int32 messageId = args->GetInt(0);
        std::map<int32, AppCallback*>::iterator it = m_mapCallbacks.find(messageId);
        
        if (it != m_mapCallbacks.end())
            ThrowJavaScriptException(it->second->GetContext(), args->GetString(1), args->GetInt(2));
    }
    
    return false;
}

void AppExtensionHandler::ThrowJavaScriptException(CefRefPtr<CefV8Context> context, CefString functionName, int retval)
{
    String code = TEXT("throw new Error('");
    switch (retval)
    {
    case ERR_INVALID_PARAM_NUM:
        code.append(TEXT("Invalid number of parameters for function "));
        code.append(functionName);
        break;
    case ERR_INVALID_PARAM_TYPES:
        code.append(TEXT("Invalid parameter types for function "));
        code.append(functionName);
        break;
    }
    code.append(TEXT("')"));
        
    CefRefPtr<CefV8Value> rv;
    CefRefPtr<CefV8Exception> exception;
        
    context->Eval(code, rv, exception);
}
#include "app.h"

#ifndef USE_WEBVIEW
#include "extension_handler.h"
#else
#include "webview_extension.h"
#endif

#include "native_extensions.h"

#include "file_util.h"
#include "network_util.h"

#ifdef OS_WIN
#include <minmax.h>
#endif


//
//
// function implementation
//
// there are the following "built-in" variables:
//
// - handler: a CefRefPtr to the client handler
// - browser: a CefRefPtr to the browser
// - state:   a CefRefPtr to the extension state, an object in which stateful
//            information and objects can be put
// - args:    the arguments passed to the function, a CefRefPtr<CefListValue>
// - ret:     the arguments that will be passed to the callback function (if any)
//
void AddNativeExtensions(NativeJavaScriptFunctionAdder* e)
{
    //////////////////////////////////////////////////////////////////////
    // Events

	// void onMenuCommand(function(string commandId))
	e->AddNativeJavaScriptCallback(
		TEXT("onMenuCommand"),
		FUNC({
			// only register callback
			return NO_ERROR;
		}
	));

    // void onAppTerminating(function())
    NativeFunction* fnxOnAppTerminating = FUNC({ return NO_ERROR; });
    fnxOnAppTerminating->SetAllCallbacksCompletedHandler(PROC({ App::QuitMessageLoop(); }));
    e->AddNativeJavaScriptCallback(TEXT("onAppTerminating"), fnxOnAppTerminating);

    
    //////////////////////////////////////////////////////////////////////
    // My Own Native Functions
    
    // In this example, we want to create the function (in C code):
    //     int myFunction(int firstNumber, int secondNumber) {
    //         return firstNumber + secondNumber;
    //     }
    //
    // The function "myFunction" will be available in your app's
    // JavaScript in the "app" object
    // (i.e., call app.myFunction(arg1, ..., argN, callback);
    // the callback function will be called once the result is ready from the
    // native layer)
    //
    e->AddNativeJavaScriptFunction(
        // function name
        TEXT("myFunction"),
                                   
        FUNC({
            // your native code here
        
            // you can access the arguments to myFunction using the
            // 'built-in' "args" array variable and set return values using
            // "ret" (which are in fact arguments passed to the callback function)
        
            int arg0 = args->GetInt(0);
            int arg1 = args->GetInt(1);
        
            // myFunction computes arg0 + arg1
            ret->SetInt(0, arg0 + arg1);
        
            // everything went OK
            return NO_ERROR;
        },
        
        // declare the argument types and names
        ARG(VTYPE_INT, "firstNumber")
        ARG(VTYPE_INT, "secondNumber")
    ));


    //////////////////////////////////////////////////////////////////////
    // Some useful functions: File System

    // void showOpenFileDialog(function(string path))
    e->AddNativeJavaScriptFunction(
        TEXT("showOpenFileDialog"),
#ifndef USE_WEBVIEW
        FUNC({
            Path path;
            if (FileUtil::ShowOpenFileDialog(path))
                ret->SetDictionary(0, path.CreateJSRepresentation());
            else
                ret->SetNull(0);
            return NO_ERROR;
		})
#else
		FUNC({
            FileUtil::ShowOpenFileDialog(callback);
            return RET_DELAYED_CALLBACK;
		})
#endif
    );

    // void showOpenDirectoryDialog(function(string path))
    e->AddNativeJavaScriptFunction(
        TEXT("showOpenDirectoryDialog"),
#ifndef USE_WEBVIEW
        FUNC({
            Path path;
            if (FileUtil::ShowOpenDirectoryDialog(path))
                ret->SetDictionary(0, path.CreateJSRepresentation());
            else
                ret->SetNull(0);
            return NO_ERROR;
		})
#else
		FUNC({
			FileUtil::ShowOpenDirectoryDialog(callback);
			return RET_DELAYED_CALLBACK;
        })
#endif
    );

    // void showInFileManager(string path)
    e->AddNativeJavaScriptProcedure(
        TEXT("showInFileManager"),
        FUNC({
            JavaScript::Object path = args->GetDictionary(0);
            FileUtil::ShowInFileManager(path->GetString("path"));
            return NO_ERROR;
        },
        ARG(VTYPE_DICTIONARY, "path")
    ));

    
    //////////////////////////////////////////////////////////////////////
    // Networking
    
#ifdef USE_WEBVIEW
    // mimicks the Zepto ajax function
    // Syntax:
    // app.ajax(options),
    // options = {
    //     type: {String, opt}, HTTP method, e.g., "GET" or "POST"; default: "GET"
    //     url: {String}
    //     data: {String, opt}, POST data
    //     contentType: {String, opt}, the content type of "data"; default: "application/x-www-form-urlencoded" if "data" is set
    //     dataType: {String, opt}, response type to expect from the server ("json", "xml", "html", "text", "base64")
    //     success: {Function(data, contentType)}, callback called when request succeeds
    //     error: {Function()}, callback called if there is an error (timeout, parse error, or status code not in HTTP 2xx)
    // }
    e->AddNativeJavaScriptFunction(
        TEXT("ajax"),
        FUNC({
            JavaScript::Object options = args->GetDictionary(0);
        
            String httpMethod = options->GetString(TEXT("type"));
            if (httpMethod == "")
                httpMethod = TEXT("GET");
        
            String url = options->GetString(TEXT("url"));
        
            String postData = options->GetString(TEXT("data"));
            String postDataContentType = options->GetString(TEXT("contentType"));
            if (postData != "" && postDataContentType == "")
                postDataContentType = TEXT("application/x-www-form-urlencoded");
        
            String responseDataType = options->GetString(TEXT("dataType"));
            if (responseDataType == "")
                responseDataType = TEXT("text");
        
            NetworkUtil::MakeRequest(callback, httpMethod, url, postData, postDataContentType, responseDataType);
        
            return RET_DELAYED_CALLBACK;
        }
        ARG(VTYPE_DICTIONARY, "options")),
        true, false,
        TEXT("return ajax(options, function(data, contentType, status) { if (status) { if (options.success) options.success(options.dataType === 'json' ? JSON.parse(data) : data, contentType); } else if (options.error) options.error(); });")
    );
#endif
}


ExtensionState::ExtensionState()
{
}
 
ExtensionState::~ExtensionState()
{
}

void ExtensionState::SetClientExtensionHandler(ClientExtensionHandlerPtr e)
{
    m_e = e;
}
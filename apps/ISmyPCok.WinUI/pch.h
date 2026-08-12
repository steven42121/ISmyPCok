#pragma once

#include <windows.h>
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>

// Undefine GetCurrentTime macro to prevent
// conflict with Storyboard::GetCurrentTime
#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Dispatching.h>

// Project's own C++/WinRT projection headers. MainWindow.xaml.g.h (generated
// by the XAML compiler Pass1) derives MainWindowT from
// winrt::ISmyPCokWinUI::implementation::MainWindow_base. That template is NOT
// in winrt/ISmyPCokWinUI.h (that header only holds the projected types); it is
// emitted by the CppWinRT component projection into Generated Files\MainWindow.g.h,
// which the XAML Pass1 output does not include. Make it visible through the PCH
// or the generated MainWindowT fails with C2143 "missing ',' before '<'".
#include <winrt/ISmyPCokWinUI.h>
#include "MainWindow.g.h"
// MainWindow.xaml.h defines winrt::ISmyPCokWinUI::implementation::MainWindow,
// which XamlTypeInfo.g.cpp references via ActivateLocalType. Without it the
// generated XamlTypeInfo.g.cpp fails with C2039 'MainWindow' is not a member
// of 'implementation'.
#include "MainWindow.xaml.h"

#include <microsoft.ui.xaml.window.h>

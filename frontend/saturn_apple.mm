#include "saturn_apple.h"

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <map>

namespace {
NSMutableArray<NSURL *> *active_urls;
std::map<std::string, std::string> resolved_paths;
NSString *const bookmark_key = @"RetroSaturn.FolderBookmarks";

void keep_access(NSURL *url)
{
    if (!active_urls) active_urls = [NSMutableArray new];
    if ([active_urls containsObject:url]) return;
    if ([url startAccessingSecurityScopedResource]) [active_urls addObject:url];
}

UIViewController *presenter()
{
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class]) continue;
        for (UIWindow *window in ((UIWindowScene *)scene).windows) {
            if (!window.isKeyWindow) continue;
            UIViewController *controller = window.rootViewController;
            while (controller.presentedViewController) controller = controller.presentedViewController;
            return controller;
        }
    }
    // SDL also supports the legacy UIApplication lifecycle without a scene manifest.
    for (UIWindow *window in UIApplication.sharedApplication.windows) {
        if (!window.isKeyWindow) continue;
        UIViewController *controller = window.rootViewController;
        while (controller.presentedViewController) controller = controller.presentedViewController;
        return controller;
    }
    return nil;
}
}

@interface SaturnDocumentPickerDelegate : NSObject<UIDocumentPickerDelegate> {
@public
    saturn::ApplePickResult result;
}
@end

@implementation SaturnDocumentPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    NSURL *url = urls.firstObject;
    if (!url) { if (result) result({}); return; }
    keep_access(url);
    NSError *error = nil;
    NSData *bookmark = [url bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:&error];
    if (bookmark) {
        NSMutableDictionary *stored = [[NSUserDefaults.standardUserDefaults dictionaryForKey:bookmark_key] mutableCopy];
        if (!stored) stored = [NSMutableDictionary new];
        stored[url.path] = bookmark;
        [NSUserDefaults.standardUserDefaults setObject:stored forKey:bookmark_key];
    }
    if (result) result(url.path.UTF8String ?: "");
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    if (result) result({});
}
@end

namespace saturn {

void apple_restore_folder_access()
{
    NSDictionary *stored = [NSUserDefaults.standardUserDefaults dictionaryForKey:bookmark_key];
    NSMutableDictionary *updated = [NSMutableDictionary new];
    for (NSString *old_path in stored) {
        NSData *bookmark = stored[old_path];
        if (![bookmark isKindOfClass:NSData.class]) continue;
        BOOL stale = NO;
        NSURL *url = [NSURL URLByResolvingBookmarkData:bookmark options:0 relativeToURL:nil bookmarkDataIsStale:&stale error:nil];
        if (!url) { updated[old_path] = bookmark; continue; }
        keep_access(url);
        resolved_paths[old_path.UTF8String] = url.path.UTF8String;
        NSData *current = stale ? [url bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil] : bookmark;
        updated[url.path] = current ?: bookmark;
    }
    [NSUserDefaults.standardUserDefaults setObject:updated forKey:bookmark_key];
}

std::string apple_resolve_path(const std::string &path)
{
    for (const auto &[old_root, new_root] : resolved_paths) {
        if (path == old_root || (path.starts_with(old_root + "/")))
            return new_root + path.substr(old_root.size());
    }
    // iOS may move an app's own container during an update.
    if (path.find("/Containers/Data/Application/") != std::string::npos) {
        const size_t documents = path.find("/Documents");
        if (documents != std::string::npos &&
            (documents + 10 == path.size() || path[documents + 10] == '/')) {
            NSString *current = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
            if (current) return std::string(current.UTF8String) + path.substr(documents + 10);
        }
    }
    return path;
}

void apple_pick_document(bool folder, const std::string &initial, ApplePickResult callback)
{
    NSString *initial_path = [NSString stringWithUTF8String:initial.c_str()];
    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController *host = presenter();
        if (!host) { callback({}); return; }
        static SaturnDocumentPickerDelegate *delegate;
        delegate = [SaturnDocumentPickerDelegate new];
        delegate->result = callback;
        NSArray<UTType *> *types = folder ? @[UTTypeFolder] : @[UTTypeData];
        UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types asCopy:NO];
        picker.allowsMultipleSelection = NO;
        picker.delegate = delegate;
        if (initial_path.length) picker.directoryURL = [NSURL fileURLWithPath:initial_path isDirectory:YES];
        [host presentViewController:picker animated:YES completion:nil];
    });
}

}

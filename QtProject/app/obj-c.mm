#import "obj-c.h"
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h> // NSOpenPanel for folder multi-selection

char* Obj_C::obj_C_addMediaToAlbum(char* albumName, char* mediaId)
{
    NSString* objAlbumName = [NSString stringWithUTF8String:albumName];
    NSString* mediaIdS = [NSString stringWithUTF8String:mediaId];

    NSString* source = [NSString stringWithFormat:@"tell application \"Photos\"\n"
                                                  @"    set selMedia to (get media items whose id contains \"%@\")\n"
                                                  @"    if not (album \"Trash from %@\" exists) then\n"
                                                  @"        make new album named \"Trash from %@\"\n"
                                                  @"    end if\n"
                                                  @"    add selMedia to album \"Trash from %@\"\n"
                                                  @"end tell",
                                                  mediaIdS, objAlbumName, objAlbumName, objAlbumName];

    NSDictionary* errorDictionary;
    NSAppleScript* script = [[NSAppleScript alloc] initWithSource:source];

    NSAppleEventDescriptor* resultDesc = [script executeAndReturnError:&errorDictionary];

    NSString* returnString = @OBJ_C_SUCCESS_STRING;
    if (resultDesc) { // was successful
        return (char*)[returnString UTF8String];
    }
    else {
        returnString = [NSString stringWithFormat:@"%@", errorDictionary];
        return (char*)[returnString UTF8String];
    }
}

char* Obj_C::obj_C_getMediaName(char* mediaId)
{
    NSString* mediaIdS = [NSString stringWithUTF8String:mediaId];

    NSString* source = [NSString stringWithFormat:@"tell application \"Photos\"\n"
                                                  @"    set selMedia to (get media items whose id contains \"%@\")\n"
                                                  @"    return filename of item 1 of selMedia\n"
                                                  @"end tell",
                                                  mediaIdS];

    NSDictionary* errorDictionary;
    NSAppleScript* script = [[NSAppleScript alloc] initWithSource:source];

    NSAppleEventDescriptor* resultDesc = [script executeAndReturnError:&errorDictionary];

    NSString* returnString = @OBJ_C_FAILURE_STRING;
    if (!resultDesc) { // failed
        return (char*)[returnString UTF8String];
    }
    else {
        returnString = [NSString stringWithFormat:@"%@", resultDesc.stringValue];
        return (char*)[returnString UTF8String];
    }
}

char* Obj_C::obj_C_revealMediaInPhotosApp(char* mediaId)
{
    NSString* mediaIdS = [NSString stringWithUTF8String:mediaId];

    NSString* source = [NSString stringWithFormat:@"tell application \"Photos\"\n"
                                                  @"    set selMedia to (get media items whose id contains \"%@\")\n"
                                                  @"    spotlight item 1 of selMedia\n"
                                                  @"    activate\n"
                                                  @"end tell",
                                                  mediaIdS];

    NSDictionary* errorDictionary;
    NSAppleScript* script = [[NSAppleScript alloc] initWithSource:source];

    NSAppleEventDescriptor* resultDesc = [script executeAndReturnError:&errorDictionary];

    NSString* returnString = @OBJ_C_SUCCESS_STRING;
    if (resultDesc) { // was successful
        return (char*)[returnString UTF8String];
    }
    else { // was an error
        returnString = [NSString stringWithFormat:@"%@", errorDictionary];
        return (char*)[returnString UTF8String];
    }
}

char* Obj_C::obj_C_selectFolders(char* initialDir)
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:NO];
    [panel setCanChooseDirectories:YES];
    [panel setAllowsMultipleSelection:YES]; // 关键：允许一次多选多个文件夹
    [panel setCanCreateDirectories:YES];
    [panel setTitle:@"Select folder(s)"];

    if (initialDir != nullptr && strlen(initialDir) > 0) {
        NSString* startPath = [NSString stringWithUTF8String:initialDir];
        [panel setDirectoryURL:[NSURL fileURLWithPath:startPath isDirectory:YES]];
    }

    if ([panel runModal] == NSModalResponseOK) {
        NSArray<NSURL*>* urls = [panel URLs];
        if (urls.count == 0) // none chosen
            return strdup("");

        NSMutableArray<NSString*>* paths = [NSMutableArray arrayWithCapacity:urls.count];
        for (NSURL* url in urls)
            [paths addObject:url.path]; // absolute folder paths, native separators

        NSString* joined = [paths componentsJoinedByString:@";"];
        return strdup([joined UTF8String]);
    }
    return strdup(""); // cancelled
}

// Modified by leooooooooo, 2026 — derivative of theophanemayaud/video-simili-duplicate-cleaner (GPL v3).
#ifndef OBJCHEADER_H
#define OBJCHEADER_H

#define OBJ_C_SUCCESS_STRING "VidSimiliSuccess" // Arbitrary success string that must be checked by caller
#define OBJ_C_FAILURE_STRING "VidSimiliFailure" // Arbitrary failure string that must be checked by caller

class Obj_C
{
  public:
    // from QT C++, convert char * with QString::fromLocal8Bit(char * stringHere)

    /* *
         * function:
         * return : string OBJ_C_SUCCESS_STRING if success, or the error if error
        * */
    static char*
    obj_C_addMediaToAlbum(char* albumName,
                          char* mediaId); //We define a static method to call the function directly using the class_name

    /* *
         * function:
         * return : media name if success, or string OBJ_C_FAILURE_STRING if error
        * */
    static char* obj_C_getMediaName(char* mediaId);

    /* *
         * function:
         * return :  string OBJ_C_SUCCESS_STRING if success, or the error if error
        * */
    static char* obj_C_revealMediaInPhotosApp(char* mediaId);

    /* *
         * function: 打开 macOS 原生目录选择面板（NSOpenPanel），允许一次多选多个文件夹
         * param  :  initialDir 初始定位目录（可为空）
         * return :  以 ";" 分隔的绝对路径列表字符串（调用方负责 free）；
         *           用户取消返回空字符串 ""；出错返回 OBJ_C_FAILURE_STRING
        * */
    static char* obj_C_selectFolders(char* initialDir);
};

#endif // OBJCHEADER_H

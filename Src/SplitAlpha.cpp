#include "imgui.h"
#include "SplitAlpha.h"
#include "Image.h"
#include "TacentView.h"
#include "FileDialog.h"

using namespace tFileDialog;
using namespace tImage;

void Viewer::DoSaveSplitAlphaModal(bool saveSplitAlphaPressed)
{
    static tString label;
    if(!CurrImage)
        return;
    if(saveSplitAlphaPressed)
    {
        SaveSplitAlphaDialog.OpenPopup(ImagesDir);
    }
    
    auto state = SaveSplitAlphaDialog.DoPopup();
    if(state == FileDialog::DialogState::OK)
    {
        auto saveDir = SaveSplitAlphaDialog.GetResult();
        auto picture = CurrImage->GetCurrentPic();
        auto profile = Config::GetProfileData();

        // rgb
        auto baseName = tSystem::tGetFileBaseName(CurrImage->Filename);
        auto rgbPngFile = saveDir + baseName + "_rgb.png";

        tPicture rgbPicture(*picture);
        for(auto x = 0; x < rgbPicture.GetWidth(); x++)
        {
            for(auto y = 0; y < rgbPicture.GetHeight(); y++)
            {
                auto pix = rgbPicture.GetPixelPointer(x, y);
                pix->A = 255;
            }
        }

        tImagePNG png(rgbPicture, false);

        auto savedFmt = png.Save(rgbPngFile);
        if(savedFmt == tImagePNG::tFormat::Invalid)
        {
            tPrintf("Failed to save image %s\n", rgbPngFile.Chr());
            return;
        }

        // alpha
        auto saPngFile = saveDir + baseName + "_sa.png";
        tPicture saPicture(*picture);
        for(auto x = 0; x < saPicture.GetWidth(); x++)
        {
            for(auto y = 0; y < saPicture.GetHeight(); y++)
            {
                auto pix = saPicture.GetPixelPointer(x, y);
                pix->R = pix->G = pix->B = pix->A;
                pix->A = 255;
            }
        }

        tImagePNG saPng(saPicture, false);

        savedFmt = saPng.Save(saPngFile);
        if(savedFmt == tImagePNG::tFormat::Invalid)
        {
            tPrintf("Failed to save image %s\n", saPngFile.Chr());
            return;
        }
    }
}
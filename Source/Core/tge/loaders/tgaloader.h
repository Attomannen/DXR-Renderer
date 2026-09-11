#ifndef _OPTIMIZABLE_TGA_H_
#define _OPTIMIZABLE_TGA_H_

class Tga32
{
public:		struct Image
			{
				unsigned short width;
				unsigned short height;
				unsigned char bitDepth;
				unsigned char *image;
				Image();
				~Image();
			};
				
				
			struct TgaHeader {
				unsigned char idLength;
				unsigned char colorMapType;
				unsigned char imageType;
				unsigned char colorMapOrigin;
				unsigned short colorMapLength;
				unsigned char colorMapEntrySize;
				unsigned short imageOriginX;
				unsigned short imageOriginY;
				unsigned short width;
				unsigned short height;
				unsigned char bpp;
				unsigned char imageDescriptor;
			};
				
				
			Tga32();
			~Tga32();
				
			static Image* Load( const wchar_t* aName );
			
};



#endif // _OPTIMIZABLE_TGA_H_

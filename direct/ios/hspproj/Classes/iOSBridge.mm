//
//		Draw lib (ios)
//			onion software/onitama 2001/6
//			               onitama 2011/5
//

//#include <stdio.h>
//#include <math.h>

#import "HspView.h"
#import "misc.h"
#import "Graphics.h"
#import "iOSBridge.h"

#include "../hsp3/supio.h"
#include "../hsp3dish/sysreq.h"

#include "../hsp3/hsp3config.h"
#include "../hsp3dish/hgio.h"
#include "../hsp3embed/asset_files.hpp"
#include "../hsp3embed/asset_bridge.hpp"
#include <memory>
#include <unistd.h>
#include <CommonCrypto/CommonDigest.h>

static std::unique_ptr<cp2::AssetFiles> cp2_asset_files;

extern "C" int cp2_initialize_assets()
{
    NSString* resources = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:@"game-assets"];
    NSString* documents = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    NSString* writable = [documents stringByAppendingPathComponent:@"DeadPatient"];
    NSError* error = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:writable withIntermediateDirectories:YES attributes:nil error:&error]) {
        fprintf(stderr, "[CP2 assets] writable directory failed: %s\n", [[error description] UTF8String]);
        return 0;
    }
    try {
        cp2_asset_files.reset(new cp2::AssetFiles([resources fileSystemRepresentation], [writable fileSystemRepresentation]));
        if (chdir(cp2_asset_files->asset_root().c_str())) return 0;
        fprintf(stderr, "[CP2 assets] original resources mounted; writes use Documents/DeadPatient\n");
        return 1;
    } catch (const std::exception& failure) {
        fprintf(stderr, "[CP2 assets] initialization failed: %s\n", failure.what());
        return 0;
    }
}

extern "C" const char* cp2_asset_read_path(const char* relative)
{
    static thread_local std::string result;
    if (!cp2_asset_files || !relative) return NULL;
    try {
        result = cp2_asset_files->read_path(relative);
        return result.empty() ? NULL : result.c_str();
    } catch (const std::exception& failure) {
        fprintf(stderr, "[CP2 assets] read rejected (%s): %s\n", relative, failure.what());
        return NULL;
    }
}

extern "C" const char* cp2_asset_write_path(const char* relative, int preserve)
{
    static thread_local std::string result;
    if (!cp2_asset_files || !relative) return NULL;
    try {
        result = cp2_asset_files->write_path(relative, preserve != 0);
        return result.c_str();
    } catch (const std::exception& failure) {
        fprintf(stderr, "[CP2 assets] write rejected (%s): %s\n", relative, failure.what());
        return NULL;
    }
}

extern "C" int cp2_asset_dirlist(const char* pattern, int mode, void* target,
                                void (*append)(void*, const char*))
{
    if (!cp2_asset_files || !pattern || !append) return -1;
    try {
        auto names = cp2_asset_files->list(pattern, mode);
        for (const auto& name : names) {
            append(target, name.c_str());
            append(target, "\n");
        }
        return (int)names.size();
    } catch (const std::exception& failure) {
        fprintf(stderr, "[CP2 assets] directory listing rejected (%s): %s\n", pattern, failure.what());
        return -1;
    }
}

extern "C" int cp2_asset_chdir(const char* path)
{
    if (!cp2_asset_files || !path) return -1;
    try { return cp2_asset_files->change_directory(path) ? 0 : -1; }
    catch (const std::exception& failure) {
        fprintf(stderr, "[CP2 assets] chdir rejected (%s): %s\n", path, failure.what());
        return -1;
    }
}

extern "C" const char* cp2_asset_current_directory()
{
    static thread_local std::string result;
    if (!cp2_asset_files) return "";
    result = cp2_asset_files->current_directory();
    return result.c_str();
}

extern "C" int cp2_assets_driver_test()
{
    // Check original bytes through the same Windows path resolver used by HSP.
    const char* resource = cp2_asset_read_path("CHAP1\\PROJECT.DAT");
    FILE* input = resource ? fopen(resource, "rb") : NULL;
    if (!input) return 0;
    CC_SHA256_CTX hash;
    CC_SHA256_Init(&hash);
    unsigned char buffer[65536], digest[CC_SHA256_DIGEST_LENGTH];
    size_t total = 0, size;
    while ((size = fread(buffer,1,sizeof(buffer),input))) {
        CC_SHA256_Update(&hash,buffer,(CC_LONG)size);
        total += size;
    }
    bool failed = ferror(input);
    fclose(input);
    CC_SHA256_Final(digest,&hash);
    char hex[65];
    for (int i=0;i<32;++i) snprintf(hex+i*2,3,"%02x",digest[i]);
    bool passed = !failed && total == 18932448 &&
        !strcmp(hex,"50d0c1db392104b2f07fa84108dd8fff2461099fa6f6a899da53d1efef14acdc");
    std::string name = ".cp2-filesystem-check-";
    name += [[[NSUUID UUID] UUIDString] UTF8String];
    const char* destination = cp2_asset_write_path(name.c_str(),0);
    std::string path = destination ? destination : "";
    FILE* output = destination ? fopen(destination,"w+b") : NULL;
    const unsigned char expected[] = {0,1,2,255,128,64,0,7};
    if (output) {
        passed = fwrite(expected,1,sizeof(expected),output)==sizeof(expected) && passed;
        passed = fclose(output)==0 && passed;
        for (char& value : name) if (value>='a' && value<='z') value -= 'a'-'A';
        const char* readback = cp2_asset_read_path(name.c_str());
        input = readback ? fopen(readback,"rb") : NULL;
        unsigned char actual[sizeof(expected)]{};
        if (input) {
            passed = fread(actual,1,sizeof(actual),input)==sizeof(actual) && passed;
            passed = !memcmp(actual,expected,sizeof(actual)) && passed;
            fclose(input);
        } else passed = false;
        passed = unlink(path.c_str())==0 && passed;
    } else passed = false;
    fprintf(stderr, "[CP2 assets test] original project hash, Windows lookup and save round trip: %s\n", passed?"PASS":"FAIL");
    return passed ? 1 : 0;
}

#define CIRCLE_DIV 16
#define DEFAULT_FONT_NAME ""
#define DEFAULT_FONT_SIZE 14
#define DEFAULT_FONT_STYLE 0

static int		nDestWidth,nDestHeight;
static int		drawflag;
static BMSCR    *mainbm = NULL;
static HspView  *hspview = NULL;

/*------------------------------------------------------------*/
/*
		iOS Base System
*/
/*------------------------------------------------------------*/

#define IMGARRAY_MAX 64

static Graphics* _g;     
static Image*    _image[IMGARRAY_MAX];
static int mouse_x, mouse_y, mouse_btn;

static EAGLContext* _context;        //コンテキスト
static GLuint       _viewRenderBuff; //レンダーバッファ
static GLuint       _viewFrameBuff;  //フレームバッファ

void gb_init( void )
{
	//グラフィックスの生成
	_g=[[Graphics alloc] init];
	
	//イメージの初期化
	for(int i=0;i<IMGARRAY_MAX;i++) {
		_image[i]=nil;
	}
}

void gb_setogl( EAGLContext *context, GLuint viewRenderBuff, GLuint viewFrameBuff )
{
    _context = context;
    _viewRenderBuff = viewRenderBuff;
    _viewFrameBuff = viewFrameBuff;
}

// The view's framebuffer for the ZGP OpenGL ES device (direct/runtime/zgp_gl.hpp),
// which draws its back buffer into it at Present.
extern "C" void cp2_view_framebuffer( unsigned int *framebuffer, unsigned int *renderbuffer )
{
    if ( _context ) [EAGLContext setCurrentContext:_context];
    *framebuffer = _context ? _viewFrameBuff : 0;
    *renderbuffer = _context ? _viewRenderBuff : 0;
}

// Presentation diagnostics: the view is presented both by the ZGP device
// (cp2_view_present) and by HSP's own canvas (gb_render_end, on redraw/mes/
// gsel), which clears to the cls color. Each switch between the two sources
// is logged (the first 40), so a ZGP frame replaced by HSP's canvas shows up.
static int cp2_last_presenter = 0;     // 1 = ZGP, 2 = HSP canvas
static unsigned cp2_presents[3] = {0, 0, 0};
static int cp2_present_switches = 0;
static void cp2_note_presenter( int source )
{
    ++cp2_presents[source];
    if ( source != cp2_last_presenter && cp2_present_switches < 40 ) {
        ++cp2_present_switches;
        fprintf( stderr, "[CP2 view] presenter %s (ZGP frames %u, HSP canvas frames %u)\n",
                 source == 1 ? "ZGP device" : "HSP canvas", cp2_presents[1], cp2_presents[2] );
    }
    cp2_last_presenter = source;
}

extern "C" void cp2_view_present( void )
{
    glBindRenderbufferOES(GL_RENDERBUFFER_OES,_viewRenderBuff);
    [_context presentRenderbuffer:GL_RENDERBUFFER_OES];
    cp2_note_presenter( 1 );
}

void gb_sethspview( void *view )
{
    hspview = (HspView *)view;
}

void gb_delimage( int i )
{
	if ( _image[i]!=nil ) {
		[_image[i] release];
		_image[i]=nil;
	}
}


void gb_bye( void )
{
	for(int i=0;i<IMGARRAY_MAX;i++) {
		gb_delimage( i );
	}
	[_g release];
}


void gb_reset( int sx, int sy )
{
    [_g initSize:CGSizeMake(sx,sy)];
    [_g setLineWidth:1];
    [_g setFontSize:14];
    [_g setColor:rgb(0,0,0)];
}


void gb_gcls( int r, int g, int b )
{
    //バッファのクリア
	[_g clear_r:r g:g b:b ];
    [_g clear];
}


void gb_dbgtest( void )
{
    gb_gcls( 0, 0, 0 );

    [_g setColor:rgb(255,255,255)];
    [_g setLineWidth:1];
    [_g drawRect_x:10 y:100 w:60 h:60];
    
    //テキストの描画
    [_g setFontSize:12];
    [_g drawString:@"HSP3Dish Ready" x:10 y:10];
}


void gb_color( int r, int g, int b )
{
    [_g setColor:rgb(r,g,b)];
}


void gb_colorset( BMSCR *bm )
{
    int r,g,b;
    r = ( bm->color >>16 ) & 0xff;
    g = ( bm->color >>8  ) & 0xff;
    b = ( bm->color ) & 0xff;
    [_g setColor:rgb(r,g,b)];
}


void gb_font( int size, int style, char *msg )
{
	//	とりあえずサイズのみ反映
	[_g setFontSize:size];
}


void gb_boxf( int x1, int y1, int x2, int y2 )
{
    [_g fillRect_x:x1 y:y1 w:x2-x1 h:y2-y1];
}


void gb_celput( int xx, int yy, int bufid, int srcx, int srcy, int sx, int sy )
{
    [_g drawScaledImage:_image[bufid]
					  x:xx y:yy w:sx h:sy
					 sx:srcx sy:srcy sw:sx sh:sy];
}


void gb_celput2( int xx, int yy, int dx, int dy, int bufid, int srcx, int srcy, int sx, int sy )
{
    [_g drawScaledImage:_image[bufid]
					  x:xx y:yy w:dx h:dy
					 sx:srcx sy:srcy sw:sx sh:sy];
}


void gb_zoom( int xx, int yy, int dx, int dy, int bufid, int srcx, int srcy, int sx, int sy )
{
    [_g drawScaledFlipImage:_image[bufid]
					  x:xx y:yy w:dx h:dy
					 sx:srcx sy:srcy sw:sx sh:sy];
}


void gb_circle( float x1, float y1, float x2, float y2, int mode )
{
	float xx,yy,rx,ry;

	rx = ((float)abs(x2-x1))*0.5f;
	ry = ((float)abs(y2-y1))*0.5f;
	xx = ((float)x1) + rx;
	yy = ((float)y1) + ry;

	if ( mode ) {
		[_g fillCircle_x:xx y:yy rx:rx ry:ry ];
	} else {
		[_g drawCircle_x:xx y:yy rx:rx ry:ry ];
	}
}


void gb_line( float x1, float y1, float x2, float y2 )
{
    [_g drawLine_x0:x1 y0:y1 x1:x2 y1:y2];
}


int gb_celload( int bufid, char *fname, int *sx, int *sy )
{
	gb_delimage( bufid );

	NSString *nsstr = [[NSString alloc] initWithUTF8String:fname];

	_image[bufid] = [[Image makeImage:[UIImage imageNamed:nsstr]] retain];
	//_image[bufid] = [[Image makeImageFromFile:nsstr] retain];

	[nsstr release];
  
    if ( _image[bufid] == nil ) return -1;

	*sx = [_image[bufid] width];
	*sy = [_image[bufid] height];
	return 0;
}


void gb_mouse( int xx, int yy, int button )
{
	mouse_x = xx;
	mouse_y = yy;
	mouse_btn = button;
    if ( mainbm != NULL ) {
        mainbm->savepos[BMSCR_SAVEPOS_MOSUEX] = xx;
        mainbm->savepos[BMSCR_SAVEPOS_MOSUEY] = yy;
    }
}

int gb_getmousex( void )
{
	return mouse_x;
}


int gb_getmousey( void )
{
	return mouse_y;
}


int gb_getmousebtn( void )
{
	return mouse_btn;
}


int gb_render_start( void )
{
	if ( GetSysReq( SYSREQ_CLSMODE ) == CLSMODE_SOLID ) {
		int ccol = GetSysReq( SYSREQ_CLSCOLOR );
		gb_gcls( (ccol>>16)&0xff, (ccol>>8)&0xff, ccol&0xff );
	}
    
    //前処理
    [EAGLContext setCurrentContext:_context];
    glBindFramebufferOES(GL_FRAMEBUFFER_OES,_viewFrameBuff);
    
    //    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR); 
    //    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); 
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST); 
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); 
   
	return 0;
}


int gb_render_end( void )
{
    //後処理
    glBindRenderbufferOES(GL_RENDERBUFFER_OES,_viewRenderBuff);
    [_context presentRenderbuffer:GL_RENDERBUFFER_OES];
    cp2_note_presenter( 2 );
    
	return 0;
}


/*------------------------------------------------------------*/
/*
		interface
*/
/*------------------------------------------------------------*/

/*------------------------------------------------------------*/
/*
 Polygon Draw Routines
 */
/*------------------------------------------------------------*/

//テクスチャ頂点情報
static GLfloat vert2D[]={
    0,  0, //左上
    0, -1, //左下
    1,  0, //右上
    1, -1, //右下
};

static GLfloat vertf2D[]={
    0,  0, //左上
    0, -1, //左下
    1,  0, //右上
    1, -1, //右下
};

//テクスチャUV情報
static GLfloat uv2D[]={
    0.0f, 0.0f, //左上
    0.0f, 1.0f, //左下
    1.0f, 0.0f, //右上
    1.0f, 1.0f, //右下
};

static GLfloat uvf2D[]={
    0.0f, 0.0f, //左上
    0.0f, 1.0f, //左下
    1.0f, 0.0f, //右上
    1.0f, 1.0f, //右下
};


int gb_mes( BMSCR *bm, char *msg )
{
	if ( bm->type != HSPWND_TYPE_MAIN ) throw HSPERR_UNSUPPORTED_FUNCTION;
    
    gb_colorset( bm );
    
    NSString *nsstr = [[NSString alloc] initWithUTF8String:msg];

    //[_g drawString:nsstr x:bm->cx y:bm->cy];
    Image *image = [_g makeTextImage:nsstr];
	[nsstr release];

    if (image==nil) return -1;
    
    GLfloat *flp;
    GLfloat x1,y1,x2,y2;
    float ratex,ratey,psx,psy;
    
    psx = image.width;
    psy = image.height;
    
    flp = vertf2D;
    x1 = (GLfloat)bm->cx;
    y1 = (GLfloat)-bm->cy;
    x2 = x1+psx;
    y2 = y1-psy;
    
    *flp++ = x1;
    *flp++ = y1;
    *flp++ = x1;
    *flp++ = y2;
    *flp++ = x2;
    *flp++ = y1;
    *flp++ = x2;
    *flp++ = y2;
    
    //ratex = 1.0f / image.width;
    //ratey = 1.0f / image.height;
    ratex = image.ratex;
    ratey = image.ratey;
    
    flp = uvf2D;
    x1 = 0.0f;
    y1 = 0.0f;
    x2 = (GLfloat)(psx * ratex);
    y2 = (GLfloat)(psy * ratey);
    
    *flp++ = x1;
    *flp++ = y1;
    *flp++ = x1;
    *flp++ = y2;
    *flp++ = x2;
    *flp++ = y1;
    *flp++ = x2;
    *flp++ = y2;
    
    glBindTexture(GL_TEXTURE_2D,image.name);
    glVertexPointer(2,GL_FLOAT,0,vertf2D);
    glTexCoordPointer(2,GL_FLOAT,0,uvf2D);
    
    [_g setBlendMode:2 alpha:255];
    //    glDisableClientState(GL_COLOR_ARRAY);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    
#if 0
	NSString *nsstr = [[NSString alloc] initWithUTF8String:msg];
	[_g drawString:nsstr x:xx y:yy];
	[nsstr release];
#endif
    bm->printsizex = psx;
    bm->printsizey = psy * 0.8f;
    return 0;
}

/*------------------------------------------------------------*/
/*
 file I/O service
 */
/*------------------------------------------------------------*/

static  char fpath_tmp[1024];

char *gb_filepath( char *base )
{
    const char* path = cp2_asset_read_path(base);
    if (path && strlen(path) < sizeof(fpath_tmp)) strcpy(fpath_tmp,path);
    else fpath_tmp[0] = 0;
    return fpath_tmp;
}


int gb_existdata(char* key)
{
	NSString *nskey = [[NSString alloc] initWithUTF8String:key];
	NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
	NSData* nsdata = [defaults dataForKey:nskey];
	[nskey release];
	if(nsdata) {
		int sizeorg = [nsdata length];
        return sizeorg;
	}
	return -1;
}


int gb_loaddata( char* key, char* data, int size, int offset )
{
	NSString *nskey = [[NSString alloc] initWithUTF8String:key];
	NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
	NSData* nsdata = [defaults dataForKey:nskey];
	[nskey release];
	if(nsdata) {
		int sizeorg = [nsdata length];
        if ( sizeorg > (size+offset) ) sizeorg = size-offset;
        unsigned char *dataorg = (unsigned char *)[nsdata bytes];
        memcpy( data, dataorg+offset, sizeorg );
        Alertf( "loaddata: %s,%d,%d", key, sizeorg, offset );
        return 0;
	}
	return -1;
}


void gb_savedata(char* key, char* data, int size, int offset)
{
    int orgsize;
    unsigned char* datatmp;
	NSString *nskey = [[NSString alloc] initWithUTF8String:key];
	NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    
    if ( offset <= 0 ) {
        orgsize = size;
        datatmp = new unsigned char[size];
        memcpy(datatmp, data, size);
    } else {
        orgsize = gb_existdata(key);
        if ( orgsize <= 0 ) orgsize = size + offset;
        if ( orgsize < (offset+size) ) orgsize = size + offset;
        datatmp = new unsigned char[orgsize];

        gb_loaddata( key, (char *)datatmp, orgsize, 0 );
        
        memcpy( datatmp+offset, data, size );
    }
    
	NSData* nsdata = [NSData dataWithBytes:datatmp length:orgsize];
	[defaults setObject:nsdata forKey:nskey];
    [defaults synchronize];
	
	Alertf( "savedata: %s,%d,%d", key, size, offset);
    
	delete datatmp;
    
	[nskey release];
}


int gb_dialog( int type, char *msg, char *msg_sub )
{
    //Alertf( "dialog: %s,%d", msg, type);
    [hspview dispDialog:type Msg:msg MsgSub:msg_sub];
    return 0;
}

int gb_exec( int type, char *name )
{
	NSString *fname = [[NSString alloc] initWithUTF8String:name];
    [[UIApplication sharedApplication] openURL:[NSURL URLWithString:fname]];
    [fname release];
    return 0;
}

void gb_nslog( char *msg )
{
    NSString *fname = [[NSString alloc] initWithUTF8String:msg];
    NSLog(fname);
    [fname release];
}

void gb_getSysModel( char *outbuf )
{
	NSString *dev = [[UIDevice currentDevice]model];
    sprintf( outbuf, "%s", [dev cStringUsingEncoding:1] );
}

void gb_getSysVer( char *outbuf )
{
	NSString *version = [[UIDevice currentDevice]systemVersion];
    sprintf( outbuf, "iOS %s", [version cStringUsingEncoding:1] );
}


void gb_getLocale( char *outbuf )
{
    NSLocale *locale = [NSLocale currentLocale];
    NSString *locstr = [locale objectForKey:NSLocaleLanguageCode];
    sprintf( outbuf, "%s", [locstr cStringUsingEncoding:1] );
    //NSLog( @"------%s",outbuf );
}


static int mestext_sx;
static int mestext_sy;

void gpb_textsize( char *msg, int fontsize, int fontstyle, int *out_x, int *out_y )
{
	NSString *text = [[NSString alloc] initWithUTF8String:msg];

	UIFont *font = [UIFont systemFontOfSize:fontsize];
    CGSize size=[text sizeWithFont:font constrainedToSize:CGSizeMake(1024,CGFLOAT_MAX)
    lineBreakMode:NSLineBreakByWordWrapping];
	mestext_sx = size.width;
	mestext_sy = size.height;
	*out_x = mestext_sx;
	*out_y = mestext_sy;
}


void gpb_textbitmap( char *msg, int fontsize, int fontstyle, char *buffer, int pitch )
{
	NSString *text = [[NSString alloc] initWithUTF8String:msg];
	UIFont *font = [UIFont systemFontOfSize:fontsize];

    //ラベルの生成
    UILabel* label=[[[UILabel alloc] init] autorelease]; 
	CGSize size=[text sizeWithFont:font constrainedToSize:CGSizeMake(1024,CGFLOAT_MAX)
        lineBreakMode:NSLineBreakByWordWrapping];

	mestext_sx = size.width;
	mestext_sy = size.height;

    [label setFrame:CGRectMake(0,0,size.width,size.height)];
    [label setText:text];
    [label setFont:font];
    //[label setTextColor:color];
    [label setTextAlignment:NSTextAlignmentLeft];
    //[label setBackgroundColor:bgcolor];
    [label setNumberOfLines:0];

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(buffer,
                                             pitch,
                                             mestext_sy,
                                             8,
                                             4 * pitch,
                                             colorSpace,
                                             kCGImageAlphaPremultipliedFirst);

    //コンテキストの設定
    UIGraphicsPushContext(context);
    CGContextTranslateCTM(context,0,mestext_sy);
    CGContextScaleCTM(context,1,-1);    
    
    //ラベルの描画
    [label.layer renderInContext:context];
    
    //コンテキストの設定解放
    UIGraphicsPopContext();
    
    //コンテキストの解放
    CGColorSpaceRelease(colorSpace);
    CGContextRelease(context);
}




// ---------------------------------------------------------------------------
// HTTP GET for the hspinet commands (netrequest_get / netexec / netgetv in
// hsp3ext_ios.cpp). One request at a time, as hspinet keeps one session.
// State: 0 in progress, 1 complete, -1 error; the body is kept as bytes.

#include <mutex>
#include <vector>

static std::mutex cp2_http_lock;
static int cp2_http_state = -1;
static std::vector<char> cp2_http_body;
static unsigned cp2_http_generation = 0;

extern "C" void cp2_http_get( const char *url )
{
    unsigned generation;
    {
        std::lock_guard<std::mutex> guard( cp2_http_lock );
        cp2_http_state = 0;
        cp2_http_body.clear();
        generation = ++cp2_http_generation;
    }
    NSString *text = [NSString stringWithCString:url encoding:NSShiftJISStringEncoding];
    NSURL *address = text ? [NSURL URLWithString:text] : nil;
    if ( !address ) {
        std::lock_guard<std::mutex> guard( cp2_http_lock );
        if ( generation == cp2_http_generation ) cp2_http_state = -1;
        fprintf( stderr, "[CP2 net] invalid URL %s\n", url );
        return;
    }
    NSURLSessionDataTask *task = [[NSURLSession sharedSession] dataTaskWithURL:address
        completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            std::lock_guard<std::mutex> guard( cp2_http_lock );
            if ( generation != cp2_http_generation ) return;
            if ( error || !data ) {
                cp2_http_state = -1;
                fprintf( stderr, "[CP2 net] GET failed: %s\n", error ? [[error localizedDescription] UTF8String] : "no data" );
                return;
            }
            const char *bytes = (const char *)[data bytes];
            cp2_http_body.assign( bytes, bytes + [data length] );
            cp2_http_state = 1;
            NSInteger status = [response isKindOfClass:[NSHTTPURLResponse class]] ? [(NSHTTPURLResponse *)response statusCode] : 0;
            fprintf( stderr, "[CP2 net] GET complete: HTTP %ld, %lu bytes\n", (long)status, (unsigned long)[data length] );
        }];
    [task resume];
}

extern "C" int cp2_http_state_get( void )
{
    std::lock_guard<std::mutex> guard( cp2_http_lock );
    return cp2_http_state;
}

// Copies the body (NUL-terminated) into a new buffer the caller frees.
extern "C" char *cp2_http_body_copy( void )
{
    std::lock_guard<std::mutex> guard( cp2_http_lock );
    char *out = (char *)malloc( cp2_http_body.size() + 1 );
    if ( !out ) return NULL;
    if ( !cp2_http_body.empty() ) memcpy( out, cp2_http_body.data(), cp2_http_body.size() );
    out[cp2_http_body.size()] = 0;
    return out;
}


// ---------------------------------------------------------------------------
// On-screen touch controls (direct/runtime/touch_controls.hpp for layout and
// keys; artwork from tools/gen_touch_art.py, embedded as PNG bytes). Drawn by
// the ZGP device after each game frame, into the view's framebuffer; a
// platform overlay, separate from the original program's rendering.

#import <GameController/GameController.h>
#include "../hsp3embed/touch_controls.hpp"
#include "../hsp3embed/touch_art_data.hpp"

namespace {

struct Cp2ArtTexture {
    GLuint name = 0;
};

enum { kArtBase, kArtKnob, kArtStickGlow, kArtPressGlow, kArtOk, kArtCancel, kArtAction, kArtMenu, kArtRun,
       kArtCount };
Cp2ArtTexture cp2_art[kArtCount];
bool cp2_art_loaded = false;

GLuint cp2_art_upload( const unsigned char *png, size_t size )
{
    NSData *data = [NSData dataWithBytesNoCopy:(void *)png length:size freeWhenDone:NO];
    UIImage *image = [UIImage imageWithData:data];
    CGImageRef cg = image.CGImage;
    if ( !cg ) return 0;
    const size_t w = CGImageGetWidth( cg ), h = CGImageGetHeight( cg );
    std::vector<unsigned char> pixels( w * h * 4, 0 );
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    // Premultiplied RGBA, drawn with GL_ONE / GL_ONE_MINUS_SRC_ALPHA.
    CGContextRef context = CGBitmapContextCreate( pixels.data(), w, h, 8, w * 4, space,
                                                  kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big );
    CGColorSpaceRelease( space );
    if ( !context ) return 0;
    CGContextDrawImage( context, CGRectMake( 0, 0, w, h ), cg );
    CGContextRelease( context );
    GLuint name = 0;
    glGenTextures( 1, &name );
    glBindTexture( GL_TEXTURE_2D, name );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
    return name;
}

void cp2_art_load( void )
{
    using namespace cp2::touch_art;
    cp2_art[kArtBase].name = cp2_art_upload( stick_base_png, stick_base_png_size );
    cp2_art[kArtKnob].name = cp2_art_upload( stick_knob_png, stick_knob_png_size );
    cp2_art[kArtStickGlow].name = cp2_art_upload( stick_glow_png, stick_glow_png_size );
    cp2_art[kArtPressGlow].name = cp2_art_upload( press_glow_png, press_glow_png_size );
    cp2_art[kArtOk].name = cp2_art_upload( ok_png, ok_png_size );
    cp2_art[kArtCancel].name = cp2_art_upload( cancel_png, cancel_png_size );
    cp2_art[kArtAction].name = cp2_art_upload( action_png, action_png_size );
    cp2_art[kArtMenu].name = cp2_art_upload( menu_png, menu_png_size );
    cp2_art[kArtRun].name = cp2_art_upload( run_png, run_png_size );
    cp2_art_loaded = true;
    fprintf( stderr, "[CP2 touch] controls ready\n" );
}

// A textured square of side `side` centred on (x, y), rotated by `degrees`
// (clockwise on screen), at opacity `alpha`.
void cp2_art_quad( GLuint texture, float x, float y, float side, float degrees, float alpha )
{
    if ( !texture ) return;
    glBindTexture( GL_TEXTURE_2D, texture );
    glColor4f( alpha, alpha, alpha, alpha );
    glPushMatrix();
    glTranslatef( x, y, 0 );
    if ( degrees != 0 ) glRotatef( degrees, 0, 0, 1 );
    const float s = side * 0.5f;
    const GLfloat positions[] = { -s, -s, s, -s, -s, s, s, s };
    const GLfloat coordinates[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    glVertexPointer( 2, GL_FLOAT, 0, positions );
    glTexCoordPointer( 2, GL_FLOAT, 0, coordinates );
    glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
    glPopMatrix();
}

}  // namespace

// A connected game controller (Xbox, PlayStation, MFi: the GameController
// framework's extended gamepad) presses the same Windows keys as the touch
// controls: d-pad or left stick = arrows, A (south) = X (OK), B (east) =
// C (cancel), X (west) = Z (action), Y (north) and Menu = S (menu), right
// shoulder or trigger = Esc (run). Polled once per presented frame. Returns
// true when the controller is in use: from its first input until the screen
// is touched again (a connected but idle controller, such as the one the
// simulator reports, leaves the touch controls on screen).
static bool cp2_pad_in_use = false;
static bool cp2_controller_poll( void )
{
    cp2::input::KeyState &keys = cp2::input::keys();
    keys.clear( cp2::input::kController );
    GCExtendedGamepad *pad = nil;
    for ( GCController *controller in [GCController controllers] ) {
        if ( controller.extendedGamepad ) { pad = controller.extendedGamepad; break; }
    }
    static bool was_connected = false;
    const bool connected = pad != nil;
    if ( connected != was_connected ) {
        fprintf( stderr, "[CP2 input] game controller %s\n", connected ? "connected" : "disconnected" );
        was_connected = connected;
    }
    if ( !pad ) {
        cp2_pad_in_use = false;
        return false;
    }
    const float threshold = 0.5f;
    const float x = pad.dpad.xAxis.value + pad.leftThumbstick.xAxis.value;
    const float y = pad.dpad.yAxis.value + pad.leftThumbstick.yAxis.value;  // up is positive
    keys.set( cp2::input::kController, 37, x < -threshold );
    keys.set( cp2::input::kController, 39, x > threshold );
    keys.set( cp2::input::kController, 38, y > threshold );
    keys.set( cp2::input::kController, 40, y < -threshold );
    keys.set( cp2::input::kController, 88, pad.buttonA.pressed );
    keys.set( cp2::input::kController, 67, pad.buttonB.pressed );
    keys.set( cp2::input::kController, 90, pad.buttonX.pressed );
    keys.set( cp2::input::kController, 83, pad.buttonY.pressed || pad.buttonMenu.pressed );
    keys.set( cp2::input::kController, 27, pad.rightShoulder.pressed || pad.rightTrigger.pressed );
    for ( int k : { 37, 38, 39, 40, 88, 67, 90, 83, 27 } )
        if ( keys.down_in( cp2::input::kController, k ) ) cp2_pad_in_use = true;
    return cp2_pad_in_use;
}

// Draws the controls over the frame in the view's framebuffer (bound by the
// caller, viewport = the whole view in pixels). While a game controller is
// in use they are hidden.
extern "C" void cp2_controls_draw( int view_width, int view_height )
{
    const bool controller = cp2_controller_poll();
    cp2::touch::controls().set_hidden( controller );
    if ( controller ) return;
    if ( !cp2_art_loaded ) cp2_art_load();
    float left = 0, right = 0, bottom = 0;
    if ( hspview ) {
        UIEdgeInsets insets = hspview.safeAreaInsets;
        const float scale = hspview.contentScaleFactor;
        left = insets.left * scale;
        right = insets.right * scale;
        bottom = insets.bottom * scale;
    }
    cp2::touch::Controls &controls = cp2::touch::controls();
    controls.layout( (float)view_width, (float)view_height, left, right, bottom );

    glMatrixMode( GL_PROJECTION );
    glPushMatrix();
    glLoadIdentity();
    glOrthof( 0, (GLfloat)view_width, (GLfloat)view_height, 0, -1, 1 );
    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glLoadIdentity();
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_CULL_FACE );
    glDisable( GL_ALPHA_TEST );
    glDisable( GL_SCISSOR_TEST );
    glEnable( GL_BLEND );
    glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
    glEnable( GL_TEXTURE_2D );
    glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
    glDisableClientState( GL_COLOR_ARRAY );
    glEnableClientState( GL_VERTEX_ARRAY );
    glEnableClientState( GL_TEXTURE_COORD_ARRAY );

    // Every texture's disc radius is cp2::touch_art::kDisc of its side.
    // Idle controls stay see-through so the game (menus at the lower left)
    // shows through them.
    const float rest = 0.5f, held = 1.0f;
    const float disc = cp2::touch_art::kDisc;
    const cp2::touch::Stick &stick = controls.stick();
    cp2_art_quad( cp2_art[kArtBase].name, stick.x, stick.y, stick.radius / disc, 0, stick.held ? 0.9f : rest );
    if ( stick.direction >= 0 ) {
        // The glow art points right; direction d turns it by 45 d degrees
        // (clockwise on screen).
        cp2_art_quad( cp2_art[kArtStickGlow].name, stick.x, stick.y, stick.radius / disc, 45.0f * stick.direction, 0.9f );
    }
    if ( stick.held )
        cp2_art_quad( cp2_art[kArtPressGlow].name, stick.knob_x, stick.knob_y, stick.knob_radius / disc, 0, 0.8f );
    cp2_art_quad( cp2_art[kArtKnob].name, stick.knob_x, stick.knob_y, stick.knob_radius / disc, 0,
                  stick.held ? held : 0.6f );
    for ( const cp2::touch::Button &b : controls.buttons() ) {
        int art = kArtOk;
        switch ( b.icon ) {
        case cp2::touch::kIconCancel: art = kArtCancel; break;
        case cp2::touch::kIconAction: art = kArtAction; break;
        case cp2::touch::kIconMenu: art = kArtMenu; break;
        case cp2::touch::kIconRun: art = kArtRun; break;
        default: art = kArtOk; break;
        }
        const float side = b.radius / disc * ( b.pressed ? 1.08f : 1.0f );
        if ( b.pressed ) cp2_art_quad( cp2_art[kArtPressGlow].name, b.x, b.y, side, 0, 0.9f );
        cp2_art_quad( cp2_art[art].name, b.x, b.y, side, 0, b.pressed ? held : rest );
    }
    glDisableClientState( GL_TEXTURE_COORD_ARRAY );
    glColor4f( 1, 1, 1, 1 );
    glMatrixMode( GL_PROJECTION );
    glPopMatrix();
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix();
}

// A touch in view pixels; phase 0 began, 1 moved, 2 ended/cancelled.
// Returns 1 when the controls take it (it is then not a game mouse touch).
extern "C" int cp2_controls_touch( long identifier, float x, float y, int phase )
{
    // Touching the screen brings the touch controls back.
    if ( phase == 0 && cp2_pad_in_use ) {
        cp2_pad_in_use = false;
        cp2::touch::controls().set_hidden( false );
    }
    return cp2::touch::controls().touch( identifier, x, y, phase ) ? 1 : 0;
}


// ---------------------------------------------------------------------------
// Text for `mes` into HSP memory bitmaps (hspwnd_dish.cpp, cp2_buffer_mes):
// one line of CP932 text in an 8-bit coverage bitmap (rows top first), its
// width the line's advance and its height the font's ascent plus descent.
// Windows renders with GDI and the named font (the game asks for
// "ＭＳ ゴシック"); iOS has neither, so a face iOS lacks becomes Hiragino Sans
// (W6 for bold, style bit 1) and glyph shapes and metrics differ from
// Windows. The HSP font size is the pixel height, used as the point size.
// Italic, underline and strike-out (style bits 2, 4, 8) are not drawn.
#include <CoreText/CoreText.h>
#include <algorithm>
#include <cmath>

bool cp2_text_coverage(const char *text, const char *font, int size, int style, std::vector<unsigned char> &coverage,
                       int &width, int &height)
{
    if (size <= 0) size = 12;
    CFStringRef string = CFStringCreateWithCString(NULL, text ? text : "", kCFStringEncodingDOSJapanese);
    if (!string) return false;
    CFStringRef requested = CFStringCreateWithCString(NULL, font ? font : "", kCFStringEncodingDOSJapanese);
    CTFontRef face = NULL;
    if (requested && CFStringGetLength(requested)) {
        CTFontRef candidate = CTFontCreateWithName(requested, size, NULL);
        CFStringRef family = candidate ? CTFontCopyFamilyName(candidate) : NULL;
        // CTFontCreateWithName substitutes a default face for unknown names.
        if (family && CFStringCompare(family, requested, kCFCompareCaseInsensitive) == kCFCompareEqualTo) {
            face = candidate;
            candidate = NULL;
        }
        if (family) CFRelease(family);
        if (candidate) CFRelease(candidate);
    }
    if (!face) face = CTFontCreateWithName((style & 1) ? CFSTR("HiraginoSans-W6") : CFSTR("HiraginoSans-W3"), size, NULL);
    if (requested) CFRelease(requested);
    const void *keys[] = {kCTFontAttributeName, kCTForegroundColorFromContextAttributeName};
    const void *values[] = {face, kCFBooleanTrue};
    CFDictionaryRef attributes = CFDictionaryCreate(NULL, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attributed = CFAttributedStringCreate(NULL, string, attributes);
    CTLineRef line = CTLineCreateWithAttributedString(attributed);
    CGFloat ascent = 0, descent = 0, leading = 0;
    const double advance = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    width = (int)std::ceil(advance);
    height = std::max(size, (int)std::ceil(ascent + descent));
    coverage.assign((size_t)std::max(width, 0) * (size_t)height, 0);
    if (width > 0) {
        // An alpha-only bitmap context: rows are stored top first.
        CGContextRef context = CGBitmapContextCreate(coverage.data(), (size_t)width, (size_t)height, 8, (size_t)width,
                                                     NULL, (CGBitmapInfo)kCGImageAlphaOnly);
        if (context) {
            CGContextSetRGBFillColor(context, 1, 1, 1, 1);
            CGContextSetTextPosition(context, 0, std::ceil(descent));
            CTLineDraw(line, context);
            CGContextRelease(context);
        }
    }
    CFRelease(line);
    CFRelease(attributed);
    CFRelease(attributes);
    CFRelease(face);
    CFRelease(string);
    return true;
}

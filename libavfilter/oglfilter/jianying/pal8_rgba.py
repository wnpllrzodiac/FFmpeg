import os

files = os.listdir('./lightning')
for f in files:
    print(f)

    os.system('../../../ffmpeg -i {0}/{2} -pix_fmt rgba {1}/{2}'.format('/home/zodiac/work/git/FFmpeg/libavfilter/oglfilter/res/lightning', '/home/zodiac/work/git/FFmpeg/libavfilter/oglfilter/res/lightning2', f))

from building import *

cwd     = GetCurrentDir()
CPPPATH = [cwd, str(Dir('#'))]

src     = Glob('*.c')


group = DefineGroup('uparam', src, depend = ['PKG_USING_UPARAM'], CPPPATH = CPPPATH)

Return('group')


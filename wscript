# -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-
VERSION = "0.1.0"
APPNAME = "ndncert"
BUGREPORT = "https://redmine.named-data.net/projects/ndncert"
GIT_TAG_PREFIX = "ndncert-"

from waflib import Logs, Utils, Context
import os

def options(opt):
    opt.load(['compiler_cxx', 'gnu_dirs'])
    opt.load(['boost', 'default-compiler-flags', 'sqlite3', 'cryptopp',
              'coverage', 'sanitizers',
              'doxygen', 'sphinx_build'],
             tooldir=['.waf-tools'])

    certopt = opt.add_option_group("ndncert options")
    certopt.add_option('--with-tests', action='store_true', default=False, dest='with_tests',
                       help='''Build unit tests''')

def configure(conf):
    conf.load(['compiler_cxx', 'gnu_dirs',
               'boost', 'default-compiler-flags', 'sqlite3', 'cryptopp',
               'doxygen', 'sphinx_build'])

    if 'PKG_CONFIG_PATH' not in os.environ:
        os.environ['PKG_CONFIG_PATH'] = Utils.subst_vars('${LIBDIR}/pkgconfig', conf.env)
    conf.check_cfg(package='libndn-cxx', args=['--cflags', '--libs'],
                   uselib_store='NDN_CXX', mandatory=True)

    conf.check_cryptopp()
    
    USED_BOOST_LIBS = ['system', 'filesystem', 'iostreams',
                       'program_options', 'thread', 'log', 'log_setup']

    conf.env['WITH_TESTS'] = conf.options.with_tests
    if conf.env['WITH_TESTS']:
        USED_BOOST_LIBS += ['unit_test_framework']
        conf.define('HAVE_TESTS', 1)

    conf.check_boost(lib=USED_BOOST_LIBS, mt=True)
    if conf.env.BOOST_VERSION_NUMBER < 105400:
        Logs.error("Minimum required boost version is 1.54.0")
        Logs.error("Please upgrade your distribution or install custom boost libraries" +
                   " (https://redmine.named-data.net/projects/nfd/wiki/Boost_FAQ)")
        return

    conf.check_compiler_flags()

    # Loading "late" to prevent tests from being compiled with profiling flags
    conf.load('coverage')

    conf.load('sanitizers')

    conf.define('SYSCONFDIR', conf.env['SYSCONFDIR'])

    # If there happens to be a static library, waf will put the corresponding -L flags
    # before dynamic library flags.  This can result in compilation failure when the
    # system has a different version of the ndncert library installed.
    conf.env['STLIBPATH'] = ['.'] + conf.env['STLIBPATH']

    conf.write_config_header('src/ndncert-config.hpp')

def build(bld):
    core = bld(
        target = "ndn-cert",
        features=['cxx', 'cxxshlib'],
        source =  bld.path.ant_glob(['src/**/*.cpp']),
        vnum = VERSION,
        cnum = VERSION,
        use = 'NDN_CXX BOOST CRYPTOPP',
        includes = ['src'],
        export_includes=['src'],
        install_path='${LIBDIR}'
    )

    bld.recurse('tools')
    bld.recurse('tests')

    bld.install_files(
        dest = "%s/ndncert" % bld.env['INCLUDEDIR'],
        files = bld.path.ant_glob(['src/**/*.hpp', 'src/**/*.h']),
        cwd = bld.path.find_dir("src"),
        relative_trick = True,
        )

    bld.install_files(
        dest = "%s/ndncert" % bld.env['INCLUDEDIR'],
        files = bld.path.get_bld().ant_glob(['src/**/*.hpp']),
        cwd = bld.path.get_bld().find_dir("src"),
        relative_trick = False,
        )

    bld.install_files("${SYSCONFDIR}/ndncert", "ca.conf.sample")
    bld.install_files("${SYSCONFDIR}/ndncert", "client.conf.sample")
    bld.install_files("${SYSCONFDIR}/ndncert", "ndncert-mail.conf.sample")

    bld(features="subst",
        source='ndncert-send-email-challenge.py',
        target='ndncert-send-email-challenge',
        install_path="${BINDIR}",
        chmod=Utils.O755,
       )

    bld(features = "subst",
        source='libndn-cert.pc.in',
        target='libndn-cert.pc',
        install_path = '${LIBDIR}/pkgconfig',
        PREFIX       = bld.env['PREFIX'],
        INCLUDEDIR   = "%s/ndncert" % bld.env['INCLUDEDIR'],
        VERSION      = VERSION,
        )

    bld.shlib(
        source=[
            "tools/ndncert-client.cpp",
            "tools/c-wrapper.cpp"], 
		target="ndncertclientshlib",
        includes=["tools"],
		use=["ndn-cert"],
        vnum = VERSION,
        cnum = VERSION,
        )

    bld.recurse('apps')

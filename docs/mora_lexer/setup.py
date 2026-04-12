from setuptools import setup

setup(
    name='mora-lexer',
    version='0.1.0',
    packages=['mora_lexer'],
    package_dir={'mora_lexer': '.'},
    entry_points={
        'pygments.lexers': [
            'mora = mora_lexer:MoraLexer',
        ],
    },
    install_requires=['pygments'],
)

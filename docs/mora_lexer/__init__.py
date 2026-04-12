"""Pygments lexer for the Mora language."""

from pygments.lexer import RegexLexer, words
from pygments.token import (
    Comment, Keyword, Name, Number, Operator, Punctuation, String, Text
)


class MoraLexer(RegexLexer):
    name = 'Mora'
    aliases = ['mora']
    filenames = ['*.mora']

    tokens = {
        'root': [
            (r'#.*$', Comment.Single),
            (r'"[^"]*"', String),
            (words((
                'namespace', 'requires', 'mod', 'not', 'or', 'in', 'dynamic',
            ), suffix=r'\b'), Keyword),
            (r'=>', Operator),
            (r'>=|<=|!=|==|>|<', Operator),
            (r':[A-Za-z_][A-Za-z0-9_]*', Name.Constant),
            (words((
                'npc', 'weapon', 'armor', 'spell', 'perk', 'keyword', 'faction',
                'race', 'leveled_list',
                'has_keyword', 'has_faction', 'has_perk', 'has_spell',
                'base_level', 'level', 'race_of', 'name', 'editor_id',
                'gold_value', 'weight', 'damage', 'armor_rating',
                'template_of', 'leveled_entry', 'outfit_has',
                'current_level', 'current_location', 'current_cell',
                'equipped', 'in_inventory', 'quest_stage', 'is_alive',
            ), prefix=r'\b', suffix=r'(?=\s*\()'), Name.Builtin),
            (words((
                'add_keyword', 'remove_keyword', 'add_item', 'add_spell',
                'add_perk', 'set_name', 'set_damage', 'set_armor_rating',
                'set_gold_value', 'set_weight', 'distribute_items',
                'set_game_setting',
            ), prefix=r'\b', suffix=r'(?=\s*\()'), Name.Function),
            (r'\b\d+\.\d+\b', Number.Float),
            (r'\b\d+\b', Number.Integer),
            (r'[A-Z][A-Za-z0-9_]*', Name.Variable),
            (r'[a-z_][a-z0-9_]*', Name),
            (r'[(),:]', Punctuation),
            (r'\s+', Text),
        ],
    }

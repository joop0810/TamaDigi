"""Simplified Chinese for every name the game shows that is not a UI string:
Digimon, attacks, items, elements, attributes, areas and avatars.

UI strings live in i18n.cpp (the ZH row); these are the DATA names, which the
other languages keep in English. gen_dex.py turns NAMES into zh_names.h (a
sorted English -> Chinese table the firmware looks up with L()), and
gen_moves.py takes MOVES for the ZH column of move_names.h.

Digimon follow the official mainland names where there is one (亚古兽, 暴龙兽,
...); the rest are rendered the same way. Nobody has proofread this: a native
speaker's corrections are welcome, and every one is a one-line change here.

Every character used here must exist in the bundled 12 px pixel font;
gen_cjk_font.py stops the build if one does not.
"""

DIGIMON = {
    'BOTAMON': '黑球兽', 'KOROMON': '滚球兽', 'AGUMON': '亚古兽', 'BETAMON': '贝塔兽',
    'GREYMON': '暴龙兽', 'TYRANNOMON': '恐龙兽', 'DEVIMON': '恶魔兽', 'MERAMON': '火焰兽',
    'AIRDRAMON': '飞龙兽', 'SEADRAMON': '海龙兽', 'NUMEMON': '鼻涕兽', 'METALGREYMON': '机械暴龙兽',
    'MAMEMON': '豆豆兽', 'MONZAEMON': '门左卫门兽', 'BLITZGREYMON': '闪电暴龙兽',
    'BANCHOMAMEMON': '番长豆豆兽', 'SHINMONZAEMON': '真门左卫门兽',
    'PUNIMON': '软软兽', 'TSUNOMON': '角角兽', 'GABUMON': '加布兽', 'ELECMON': '电击兽',
    'KABUTERIMON': '比多兽', 'GARURUMON': '加鲁鲁兽', 'ANGEMON': '天使兽', 'FRIGIMON': '雪人兽',
    'BIRDRAMON': '巨鸟兽', 'WHAMON': '巨鲸兽', 'VEGIEMON': '蔬菜兽', 'SKULLGREYMON': '骷髅暴龙兽',
    'METALMAMEMON': '机械豆豆兽', 'VADEMON': '外星兽', 'SKULLMAMMON': '骷髅猛犸兽',
    'CRESGARURUMON': '月光加鲁鲁兽', 'EBEMON': '异星兽',
    'POYOMON': '波波兽', 'TOKOMON': '多高兽', 'PATAMON': '巴达兽', 'KUNEMON': '虫虫兽',
    'UNIMON': '独角兽', 'CENTARUMON': '半人马兽', 'OGREMON': '鬼兽', 'BAKEMON': '幽灵兽',
    'SHELLMON': '贝壳兽', 'DRIMOGEMON': '钻地兽', 'SUKAMON': '大便兽', 'ANDROMON': '仿生兽',
    'GIROMON': '陀螺兽', 'ETEMON': '猩猩兽', 'CHIMAIRAMON': '奇美拉兽', 'HIANDROMON': '高仿生兽',
    'GOKUMON': '悟空兽', 'BANCHOLEOMON': '番长狮子兽', 'MILLENNIUMON': '千年兽',
    'YURAMON': '幽幽兽', 'TANEMON': '种子兽', 'BIYOMON': '比丘兽', 'PALMON': '巴鲁兽',
    'MONOCHROMON': '独角龙兽', 'COCKATRIMON': '鸡蛇兽', 'LEOMON': '狮子兽', 'KUWAGAMON': '锹形虫兽',
    'COELAMON': '腔棘鱼兽', 'MOJYAMON': '毛毛兽', 'NANIMON': '纳尼兽', 'MEGADRAMON': '超级飞龙兽',
    'PICCOLOMON': '短笛兽', 'DIGITAMAMON': '数码蛋兽', 'DARKDRAMON': '暗黑龙兽',
    'BLOOMLORDMON': '花王兽', 'GANKOOMON': '顽固兽',
    'ZURUMON': '狡猾兽', 'PAGUMON': '巴古兽', 'GAZIMON': '加治兽', 'GIZAMON': '锯齿兽',
    'DARKTYRANNOMON': '黑暗恐龙兽', 'CYCLOMON': '独眼兽', 'DEVIDRAMON': '恶魔龙兽',
    'TUSKMON': '长牙兽', 'FLYMON': '飞虫兽', 'DELTAMON': '三头兽', 'RAREMON': '腐烂兽',
    'METALTYRANNOMON': '机械恐龙兽', 'NANOMON': '纳米兽', 'EXTYRANNOMON': '超级恐龙兽',
    'MACHINEDRAMON': '机械邪龙兽', 'RAIDENMON': '雷电兽', 'GAIOUMON': '凯王兽',
}

# Attacks, by their internal name (MOVES[i][0] in digimon_data.py).
MOVES = {
    'BUBBLE BLOW': '泡泡攻击', 'TACKLE': '撞击', 'CLAW ATTACK': '利爪攻击', 'QUICK DASH': '快速冲刺',
    'HEADBUTT': '头槌', 'BODY PRESS': '泰山压顶', 'DATA CRUSH': '数据粉碎', 'GUARD': '防守',
    'POWER CHARGE': '蓄力', 'FOCUS': '集中', 'GROWL': '吼叫', 'RECOVERY': '恢复',
    'PEPPER BRTH': '小型火焰', 'ELEC SHOCK': '电击', 'GREAT HORNS': '巨角攻击', 'MEGA FLAME': '超级火焰',
    'TAIL SMASH': '尾巴重击', 'BLAZE BLAST': '烈焰冲击', 'TOUCH EVIL': '邪恶之触', 'DEATH CLAW': '死亡之爪',
    'ROAR FIRE': '咆哮之火', 'BURN FIST': '燃烧之拳', 'WING SLASH': '翼刃', 'SPIN NEEDLE': '旋转针',
    'WATER JET': '水柱', 'ICE BLAST': '冰之冲击', 'NUME SLUDGE': '鼻涕弹', 'MEGA CLAW': '巨爪',
    'GIGA DESTROY': '千兆毁灭', 'MAME PUNCH': '豆豆拳', 'SMILEY BOMB': '笑脸炸弹', 'LOVELY PUNCH': '可爱拳',
    'HEARTS ATK': '爱心攻击', 'PETIT FIRE': '小火焰', 'SUPER THUNDR': '超级雷电', 'HORN CHARGE': '角之冲锋',
    'ELECTRO SHOK': '电流冲击', 'FOX FIRE': '狐火', 'HOWLING BLST': '咆哮冲击', 'ANGEL ROD': '天使之杖',
    'HAND OF FATE': '天堂之拳', 'SNOW BALL': '雪球', 'SUBZERO FIST': '零度拳', 'FIRE WING': '火之翼',
    'METEOR WING': '流星之翼', 'BLASTING SPT': '喷水冲击', 'TIDAL WAVE': '海啸', 'SWEET BREATH': '甜蜜吐息',
    'BONE CRUSH': '碎骨', 'GROUND ZERO': '零点爆破', 'METAL PUNCH': '金属拳', 'ENERGY BOMB': '能量炸弹',
    'UF KISS': '飞碟之吻', 'ABDUCT BEAM': '绑架光线', 'BOOM BUBBLE': '空气炮', 'ELEC THREAD': '电丝',
    'HORN STRIKE': '角之一击', 'AERIAL ATK': '空中攻击', 'SOLAR RAY': '太阳光线', 'HUNT CANNON': '狩猎炮',
    'BONE CUDGEL': '骨棒', 'PUMMEL WHACK': '连续猛击', 'HYPNOTISM': '催眠', 'DARK CLAW': '暗黑之爪',
    'GOLD RUSH': '黄金冲击', 'HYDRO PRESS': '水压', 'CRUSH DRILL': '粉碎钻头', 'IRON DRILL': '钢铁钻头',
    'POOP THROW': '大便攻击', 'LIGHTN BLADE': '闪电刃', 'GATLING MSL': '加特林导弹', 'SPIRAL SAW': '螺旋锯',
    'DEADLY BOMB': '致命炸弹', 'LOVE SEREN': '爱之小夜曲', 'DARK SPIRITS': '暗黑之魂', 'POWER GUILLO': '力量断头台',
    'SCISSOR CLAW': '剪刀爪', 'HORN RUSH': '角之猛冲', 'VOLCANO STRK': '火山冲击', 'PIT PELTER': '石块投掷',
    'BIT BOMB': '比特炸弹', 'TRIDENT ARM': '三叉戟臂', 'BLITZ CANNON': '闪电炮', 'BANCHO PUNCH': '番长拳',
    'GUTS BOMB': '斗志炸弹', 'HUG TACKLE': '拥抱冲撞', 'HEART BREAK': '心碎', 'TUSK CRUSH': '獠牙粉碎',
    'SKULL STAMP': '骷髅践踏', 'LUNAR CLAW': '月光之爪', 'FROST HOWL': '冰霜咆哮', 'BRAIN BEAM': '脑波光线',
    'BRAIN RUPTR': '脑波破坏', 'DEMON SNAKE': '恶魔之蛇', 'HEAT VIPER': '热能毒蛇', 'SPIRAL SWORD': '螺旋剑',
    'ATOMIC RAY': '原子光线', 'HELL CHAIN': '地狱锁链', 'HELL FIRE': '地狱之火', 'BANCHO FIST': '番长铁拳',
    'BURN BANCHO': '燃烧番长', 'TIME BURST': '时间爆破', 'TIME DESTROY': '时间毁灭', 'SPIRAL TWIST': '螺旋扭转',
    'POISON IVY': '毒藤', 'PECK DRILL': '啄钻', 'PETRIFY GAZE': '石化凝视', 'BEAST SWORD': '兽王剑',
    'BEAST FIST': '兽王拳', 'SHELL STRIKE': '贝壳冲击', 'TIDE CRASH': '潮汐冲击', 'BONE BOOMER': '骨头回旋镖',
    'ICE CLOUD': '冰云', 'NANI PUNCH': '纳尼拳', 'DARK CLAW S': '暗黑利爪', 'GENOCIDE ATK': '毁灭攻击',
    'EGG BOMB': '蛋壳炸弹', 'NIGHTMARE': '噩梦', 'DARK LANCE': '暗黑长枪', 'GIGA DEATH': '千兆死亡',
    'BLOOM THORN': '花之荆棘', 'PETAL STORM': '花瓣风暴', 'IRON FIST': '铁拳', 'GANKOO SMASH': '顽固重击',
    'STUN BLAST': '麻痹冲击', 'SAW SPIN': '旋转锯', 'FIRE BLAST': '火焰爆破', 'BEAM ARM': '光束臂',
    'HYPER HEAT': '超高热', 'RED EYE': '红眼', 'CRIMSON CLAW': '深红之爪', 'HORN ATTACK': '角攻击',
    'SLAM TUSK': '獠牙猛击', 'POISON POWDR': '毒粉', 'DEADLY STING': '致命毒刺', 'SNAKE BITE': '蛇咬',
    'TRIPLE FORCE': '三重力量', 'TOXIC SLUDGE': '剧毒淤泥', 'MISSILE RAIN': '导弹雨', 'NUKE LASER': '核能激光',
    'NANO TWIST': '纳米旋风', 'PLUG BOMB': '插头炸弹', 'DINO KICK': '恐龙踢', 'PYRO BLASTER': '火焰喷射',
    'GIGA CANNON': '千兆炮', 'INFINITY CAN': '无限加农炮', 'THUNDR LANCE': '雷电长枪', 'RAIDEN STORM': '雷电风暴',
    'TWIN BLADE': '双刃', 'METEOR SLASH': '流星斩', 'STRUGGLE': '挣扎',
}

ITEMS = {
    'SCAN CHIP': '扫描芯片', 'SUPER SCAN': '超级扫描', 'HYPER SCAN': '高级扫描', 'MASTER SCAN': '大师扫描',
    'SM RECOVERY': '小恢复', 'MD RECOVERY': '中恢复', 'LG RECOVERY': '大恢复', 'ALL CURE': '万能药',
    'ATTACK CHIP': '攻击芯片', 'DEFENSE CHIP': '防御芯片', 'SPEED CHIP': '速度芯片',
}

ELEMENTS = {
    'NEUTRAL': '无', 'FIRE': '火', 'WATER': '水', 'PLANT': '植物', 'ELECTRIC': '电',
    'WIND': '风', 'EARTH': '大地', 'LIGHT': '光', 'DARK': '暗',
}

ATTRS = {'VACCINE': '疫苗种', 'DATA': '数据种', 'VIRUS': '病毒种', 'FREE': '自由种'}

AREAS = {
    'NATIVE FOREST': '原始森林', 'GEAR SAVANNA': '齿轮草原', 'DRILL TUNNEL': '钻头隧道',
    'TROPIC JUNGLE': '热带丛林', 'OVERDELL': '亡者之谷', 'FREEZELAND': '冰冻之地',
    'MT. PANORAMA': '全景山', 'GREYLORD HALL': '灰王殿', 'ELITE': '精英', 'CHAMPION': '冠军',
    'PRIMARY VLG': '起始村', 'MISTY TREES': '迷雾森林', 'GECKO SWAMP': '壁虎沼泽',
    'BEETLE LAND': '甲虫之地', 'GREAT CANYON': '大峡谷', 'ICE SANCTUARY': '冰之圣域',
    'DATAMON RUIN': '数据遗迹', 'FACTORIAL': '工厂镇', 'TOY TOWN': '玩具镇', 'SPIRAL MTN': '螺旋山',
    'LOST VILLAGE': '失落之村', 'SERVER DESERT': '服务器沙漠', 'GEAR CITY': '齿轮城',
    'DARK FOREST': '黑暗森林', 'KOROMON VLG': '滚球兽村', 'INFINITY MTN': '无限山',
    'FLOWER FIELD': '花田', 'BIRD CLIFF': '飞鸟崖', 'BEAST PLAINS': '野兽平原',
    'INSECT WOODS': '昆虫森林', 'ANCIENT SEA': '远古之海', 'SNOW VALLEY': '雪之谷',
    'TOY BOX': '玩具箱', 'EGG TEMPLE': '蛋之神殿', 'SEWER': '下水道', 'JUNK YARD': '废物场',
    'DINO GORGE': '恐龙峡谷', 'SWAMP OF FLIES': '苍蝇沼泽', 'DEVIL PEAK': '恶魔峰',
    'TRIPLE CAVE': '三重洞窟', 'METAL CITY': '金属城', 'MACHINE CORE': '机械核心',
}

AVATARS = {
    'GOGGLES': '护目镜', 'CAP': '帽子', 'LONG HAIR': '长发', 'SPIKY': '刺猬头',
    'GOLD': '金色', 'TEAL': '青色', 'ROSE': '玫瑰', 'NIGHT': '夜色',
}

# Region labels: the versions stay "VER.1".."VER.5", the catch-all is translated.
MISC = {'ALL': '全部'}

# English display string -> Chinese, for L(). Moves are not in here: they have
# their own per-language table (move_names.h).
NAMES = {}
for table in (DIGIMON, ITEMS, ELEMENTS, ATTRS, AREAS, AVATARS, MISC):
    for k, v in table.items():
        if NAMES.get(k, v) != v:
            raise SystemExit('two Chinese names for %s' % k)
        NAMES[k] = v

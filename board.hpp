#pragma once

#include<vector>
#include<unordered_map>
#include<cstdint>
#include<array>
#include<string>

using Hash = std::uint64_t;

inline const int INF = 1e9, NEGINF = -1e9;

enum class SpecialMove : std::uint8_t {
    None = 0,
    CastleKingside = 1,
    CastleQueenside = 2,
    PromoteQueen = 3,
    PromoteRook = 4,
    PromoteBishop = 5,
    PromoteKnight = 6
};

struct Move {
    int old_x = -1;
    int old_y = -1;
    int new_x = -1;
    int new_y = -1;
    SpecialMove special_move = SpecialMove::None;

    bool operator==(const Move& other) const {
        return old_x == other.old_x && old_y == other.old_y
            && new_x == other.new_x && new_y == other.new_y
            && special_move == other.special_move;
    }

    bool operator!=(const Move& other) const {
        return !(*this == other);
    }
};

enum class CastlingRight : std::uint8_t {
    WhiteKingside = 1u << 0,
    WhiteQueenside = 1u << 1,
    BlackKingside = 1u << 2,
    BlackQueenside = 1u << 3
};

class Board{
    friend struct BoardTestAccess;

    // Fixed SplitMix64 values, seed 0x31802627; piece index is (piece + 6) * 64 + square.
    inline static constexpr std::array<Hash, 832> kPieceHashes{{
        0x57582e9519ca9983ULL, 0x334a3c775f1c3260ULL, 0xd771cbfc63a87b66ULL, 0x32008bb3dc9afe42ULL,
        0x7de4245ac67c2e99ULL, 0xf427ff603c436715ULL, 0xa353ce2a900d231aULL, 0x37df79ae17d9acb6ULL,
        0xf34555963d6f678fULL, 0x2ac8b483146dfb83ULL, 0x8d875b500e084481ULL, 0xa8dc7c0cab7ff6b6ULL,
        0xb07980afe520f277ULL, 0x9885e12e65ee44a3ULL, 0xd4bf9d647f35a5f7ULL, 0x757264220702b051ULL,
        0x8fcdad0a3c432239ULL, 0x210ef799e247ca75ULL, 0x5498825a3b402db1ULL, 0x9ba167267fe3eb38ULL,
        0xf7dfd8481010f46eULL, 0xba85b9002ad682a3ULL, 0x0da552d24368a37aULL, 0x75abb00e7c208d1dULL,
        0xca1ddc52e5fa8295ULL, 0x153940759c27cf04ULL, 0xebb0b6464388cb50ULL, 0xacd98d1db07f1dc4ULL,
        0x285ec691a9dd2af1ULL, 0x2a04f1f73b93d6f3ULL, 0x2598d51d1d5eb1b3ULL, 0x1f0f6b14f9631f5fULL,
        0x71a0e4fdeb1d3cbbULL, 0xa054b7f206a3288dULL, 0x3830ab153dca2ac2ULL, 0x0338c87671bd26acULL,
        0x66479f85780e4fe5ULL, 0x952b8c38240d6d0fULL, 0x6fe6edccbd1502acULL, 0x62e07dd6b8e5820fULL,
        0xa27093fa72cb1a14ULL, 0x5dcb04ba9dfae974ULL, 0x0576aeeddbecdecbULL, 0xf9e052b0b4d78e6eULL,
        0x127890806e86720eULL, 0x4e75e4ce22970fedULL, 0xb073bb6834f9b320ULL, 0x9b7f01962694db95ULL,
        0x82fd00538fabe581ULL, 0x086c862c5bae32b1ULL, 0xbbb0352a5b6e8965ULL, 0x484c3270ed7e4821ULL,
        0xe924febdb1942d4fULL, 0xc21c945518d7dfddULL, 0x55ea05639edb35a3ULL, 0x40310f26542a975aULL,
        0x6eb40ca6642aea9bULL, 0x99236d5fee14039cULL, 0xe395202c9f249817ULL, 0x5841d25021996bd6ULL,
        0xa845b4d3699d3853ULL, 0x87ba7ef3a4330aa8ULL, 0x3e90c5ab934fc393ULL, 0x521854ac0d41785cULL,
        0xdf8b243518b4433cULL, 0x3b312bfd8d0b9e53ULL, 0x877cab2d03655248ULL, 0x7dc1eff3407af5d6ULL,
        0xd80f843ec406f44cULL, 0xff7517fd0015c26cULL, 0xfb39938a7c698f8eULL, 0xc7880acd76b27768ULL,
        0xb1c3348d29d91b4fULL, 0xbd5af5fddd21587bULL, 0x36bbc67c72de56ecULL, 0x7786da79a5bf0d36ULL,
        0x0b10b1a65a8fa3ecULL, 0x62c449ed72e1afb6ULL, 0x6c27aec21e738dadULL, 0x5c52a4b2945a5dddULL,
        0x36a2b3e98b18fa3bULL, 0x8a3621be32a14bc3ULL, 0x58712377263b0cebULL, 0x712a2f7309610b68ULL,
        0x5da591122f212ca4ULL, 0xc26b16092a70aed0ULL, 0x2e28f855392bad0eULL, 0x5d985b4159b1c7a0ULL,
        0x585d018661074848ULL, 0xdbc6696ae825b738ULL, 0x31a012f3580983aaULL, 0x5f1edcbf7c6ebca6ULL,
        0x1edc225bbd7e4cb8ULL, 0x940512801f225028ULL, 0x8bd0dec8050283c8ULL, 0x0486f113fe6901a8ULL,
        0x7b298b8e4ae9818cULL, 0x083df4b8735e4b85ULL, 0xcaef4c24317c4094ULL, 0x7742aafbe81716a8ULL,
        0x4fc1fa9e43ed672fULL, 0x742e40ccb0273519ULL, 0x70f5857807c53ad9ULL, 0x406d6e8e3c267bafULL,
        0xd58bc62292b7dca9ULL, 0xc738677813805bd5ULL, 0x200d11646dc6f7d6ULL, 0x8e7d0859106a1902ULL,
        0xc5ab0a08de74d37bULL, 0x2d7725aad03f3878ULL, 0x3f1a8007ea22218bULL, 0xc64399f83872070aULL,
        0xcdcbb3ba6acad2e4ULL, 0x343889f317855d50ULL, 0x1b24e36426451d12ULL, 0xe82759da425fe7a4ULL,
        0x6a7e1362e0e283abULL, 0x28de5f967d52a328ULL, 0x97a7a6ccd322b0a8ULL, 0xc2653c76910b16d9ULL,
        0xaf03ea675c19bc24ULL, 0x4fb33540320e59acULL, 0xe01c28d5c059aee7ULL, 0xd1e742fb077c0f78ULL,
        0x13b56b053399df2fULL, 0x2b287c7563acb596ULL, 0x01b3ae115a63552fULL, 0x2002e04345bffbceULL,
        0x6ddfd5bed6b9d0beULL, 0xa51abf9472f69e31ULL, 0xc7cabafacf9a2585ULL, 0xf52a0788ff3d7257ULL,
        0x0dd00f88f05abce3ULL, 0xf9d66eb05bb394c4ULL, 0x70669f2f43498aa2ULL, 0x1a6b26a5021d5e1dULL,
        0xd93da539cc6a0d71ULL, 0x0587ac4e343e3eaeULL, 0x8fb221c85bc5f236ULL, 0xbe08c6a26d2834cfULL,
        0x0c944a6f0d01351fULL, 0x745c8032a7d3d308ULL, 0xf0047857dbc22f41ULL, 0xbb313ac993a6db22ULL,
        0x5d2c4d3c173de372ULL, 0x1652987f8d62a15eULL, 0x4a01434f128c986aULL, 0x01313a08ff0d7e3bULL,
        0xebe330dfaa43f011ULL, 0xd569cb1b497d31f9ULL, 0x94448f2a0afbf796ULL, 0x94461c2b54edf9deULL,
        0x0f1e122620c49c28ULL, 0xf533150a3f3d6bf4ULL, 0xab18da035c4ce5ecULL, 0x34bb6b4da4380ef5ULL,
        0xd859de98355db3b4ULL, 0xfd34b4462fc7fa56ULL, 0x149f2291cb835eaaULL, 0xf14e8b2e714dfca3ULL,
        0x36410816271b4109ULL, 0xb36b6787e32d1f8dULL, 0xd83c65df325576c5ULL, 0x5e833b57aaa35254ULL,
        0xe9466b1710adbd19ULL, 0x00170d3ec560828bULL, 0xcbdffb95cb37d1f2ULL, 0xf75ceba80272de9cULL,
        0xe43bb99118bbcf75ULL, 0x3a49d168f9822b4bULL, 0xcd9ace1f519059e5ULL, 0x58f74ce0d1fe6965ULL,
        0x7c60c2bb840d317cULL, 0x71c4c1f21d638538ULL, 0x4126141fddd647a9ULL, 0x4a703a5e9484dee5ULL,
        0xca56a5cdd615ed6aULL, 0x18041f8046d24abbULL, 0x0cfcd83257a00ec7ULL, 0x2c4134170e7c5a06ULL,
        0x4a2a4717e34ed13eULL, 0xad367fb277aaa01fULL, 0xacdc67cbf8328ac6ULL, 0xd74ae5bd262f885fULL,
        0x9c4f9ac293bbb738ULL, 0x995745fb04855726ULL, 0x123b29a23d7b33e6ULL, 0x231fac8fb9dcf582ULL,
        0xc017265104915130ULL, 0xbc78d089ac359499ULL, 0x48bf4c0c587a8ebaULL, 0x7adfe33c8d71f1b8ULL,
        0xfe9698bba7f0f0ecULL, 0x49a404757477767fULL, 0x850caa9355342083ULL, 0x10478232c13169f5ULL,
        0x336878ad768bcd5aULL, 0xc42498a6c82bbd47ULL, 0x4bde2a94a0950fb8ULL, 0xc428aa6d57ebe7eeULL,
        0x86cb255015ef8176ULL, 0x21492040e0c6bc28ULL, 0x27e0ee8bcf1a8e6cULL, 0x5762349c8b855f93ULL,
        0xe34552a4d4b36346ULL, 0x2a2e218d5f7bdc84ULL, 0xc684ee0b637f0806ULL, 0x3726b242f666cadeULL,
        0xff0743dd7966055bULL, 0x7d3abbb0b65893a3ULL, 0x8a10df81992b2a30ULL, 0xc23ed6111ca8755eULL,
        0x915b6c8e1bb7fd3dULL, 0x1c4ac7f095e6fe5aULL, 0xe7569c5d95095127ULL, 0xd9d0394fe11e2a74ULL,
        0xfa2af7c600f9832aULL, 0x7d1891ea81b5bed4ULL, 0x663b56af7d7e7e8aULL, 0x8999848e8ebe87d5ULL,
        0x41545dae91aabe93ULL, 0xdad74b073b42d15eULL, 0x4407f4930f5c0ba5ULL, 0xeb19652578f87534ULL,
        0x94044ded09fc293dULL, 0xc80bca13827f51bbULL, 0x27383f15ee25b9d8ULL, 0xf0f9b821db614c65ULL,
        0xd68048138e7184d7ULL, 0x1da334473da5b174ULL, 0x70122d70afef29beULL, 0x5f29554665ffce49ULL,
        0xd62813b0219d24e2ULL, 0xce62f5965434dd1dULL, 0xb6c62997288a89b8ULL, 0xcc2d7cb7fab6d2bfULL,
        0xef75ff2c8a83f895ULL, 0x63726d7fa4888214ULL, 0x22934b35a10860baULL, 0xcd6a490b5a6fd889ULL,
        0x4c10de6865959b3eULL, 0xf98cc8a9718e1b18ULL, 0xd52e462d49bebe9bULL, 0x8a9a197cb177ee50ULL,
        0x02dd018f4165eae4ULL, 0xad5ebd4323b9b066ULL, 0x36d237428a48eecbULL, 0xa807b9e8abb55380ULL,
        0xfec6c2c83dc9a07aULL, 0x132e2c37b642314cULL, 0x57340b41e16d5659ULL, 0x1600f847ee130152ULL,
        0xba39b7cc78fe4141ULL, 0x097cb0a6f66bd72cULL, 0x389ed9c30571bcd4ULL, 0x4d5a3f6042f18723ULL,
        0x01a284c9128ec5cdULL, 0x47cf3cb77f8d3530ULL, 0x79f27ab186db3fe5ULL, 0x9bd3592b8054062fULL,
        0xbe7ba2941367799bULL, 0x8f4e27d470a45b75ULL, 0xb3d41f274941f66cULL, 0x096f6b5899c3c1cbULL,
        0x9f1628ccfee70a66ULL, 0x27396e467530ea07ULL, 0x66b5fdd30feeec34ULL, 0xa5f6faa7dd63a71bULL,
        0x3396073c7a3f20ecULL, 0xfbdbfebfc3888135ULL, 0xdb57f88eb004f28fULL, 0x98f6308083d009b4ULL,
        0xbe189268094d4c4dULL, 0x436aa7f39c00dc40ULL, 0x3fdaca7657420a69ULL, 0xc790e3d00ba14c89ULL,
        0x8aa312aca2215370ULL, 0x07d09dfdbc033a48ULL, 0x6132edfe4ddaa5f9ULL, 0x9e43be7cc44d4abbULL,
        0x7d4a4a7c6c1bb216ULL, 0x7c057f476fba9c57ULL, 0x28ba458eeb2f82ffULL, 0x4e4f3907e3143f68ULL,
        0xa23ee7a30782b083ULL, 0x211a7d7c65b9247fULL, 0xf04a6454c024bbf3ULL, 0x24ba0b3b2b5960f9ULL,
        0xb7518b20f11fff62ULL, 0xd9d7517c71b9a540ULL, 0xf5af6d66b5cbd770ULL, 0x5a090dcdfcac3564ULL,
        0x9a4682ecd65e0cacULL, 0x7d9242be31fe8c93ULL, 0xffb3149c63dffe55ULL, 0x5a88dc7e85d39094ULL,
        0x4a50d9f80b217741ULL, 0x948fdfef958eb9d6ULL, 0xeda70dc47f8edc6cULL, 0xb66fa1b7536abb77ULL,
        0x59ffb0a98589cfeaULL, 0x892059d3010d990dULL, 0xf2581975b5ef9392ULL, 0x9eabb726aa8392abULL,
        0x2237e88714d2076cULL, 0x64e46c651cca8b9eULL, 0x3af8393190203bb3ULL, 0x3daab7c48c029cc2ULL,
        0xc00b0281eed6dfdfULL, 0xdeb8a3544a8a5dffULL, 0x4040dc59e7b98cbfULL, 0x05c5cd7081fe8fa6ULL,
        0x9f6d848ed0108490ULL, 0xd32a70df0b2c99bbULL, 0xac771e8073c71455ULL, 0x57d91fb691d447c6ULL,
        0x6c48fbd15bc71b31ULL, 0x7db0ee509aa3cd5aULL, 0xfe7433de6ebd71bbULL, 0xb24a76693817bc50ULL,
        0x42798290928aaefaULL, 0xa17c5ec967cf2676ULL, 0xc8c9811ee8cdb14bULL, 0xd3659f734e5905d2ULL,
        0x9ecf540e261fe5f6ULL, 0x3974ab57491fe909ULL, 0x8a3136cd4911d060ULL, 0xe3ba0a11b373c7cdULL,
        0xa61f14064dc6f4a0ULL, 0xca1b3ddb74332d8eULL, 0xdb6a17032cf0f2dfULL, 0x1b7ba7aed5e9a985ULL,
        0x3eec69745939173aULL, 0xb795f5059bf2aa6eULL, 0x549a2770709449eeULL, 0x79804798734499f1ULL,
        0x259d2502058b9c44ULL, 0x2cec7e2bf03e8891ULL, 0x0216d5c07c7d7b30ULL, 0x13df759af320e438ULL,
        0xa18951dc2a209193ULL, 0xa11d6a70d0c00846ULL, 0xe742894e3a8e1bbeULL, 0x486a3779507d75e9ULL,
        0x6b1b6dc4f20882d4ULL, 0x17690001a9265ccaULL, 0xe282fb8bc9f04018ULL, 0x9f2172e895d1f1c5ULL,
        0xa086d4bf1486f5d7ULL, 0x9e179c25a5893161ULL, 0x29c7694613e80819ULL, 0x3e619ddc68b2db62ULL,
        0x16233bcd5a6cc967ULL, 0x979358a3548df24dULL, 0x20a976fc8fcdd1edULL, 0xf7a98b4567dcc820ULL,
        0xa763e3e54754d637ULL, 0x23ce67b1d848711bULL, 0x55348d3f639bc480ULL, 0x5446ffacee0d2562ULL,
        0x2cff5b623247f705ULL, 0xc1ed902fbef43ab9ULL, 0x2a45e8e02c4e9280ULL, 0xdd6d3621bb448e93ULL,
        0xbc96e3a212464a7dULL, 0xa5b66d63618ee060ULL, 0x46531215dc6014bfULL, 0x495233f17ec016a7ULL,
        0x297bde656082ccdfULL, 0x5602e20916c7a4c0ULL, 0xc1733cfbda0db3b1ULL, 0xd27e43170e01af10ULL,
        0xf4c80739b3dc1665ULL, 0x9e8a4581c9e180a5ULL, 0x4340109582e1ee80ULL, 0x5c4a720dcd4a3d58ULL,
        0x853f10ea5a66ee91ULL, 0xee805001961633f5ULL, 0x02ae614ac8854552ULL, 0x7023ad0459240066ULL,
        0x887483b5531daac6ULL, 0x2a8ac953c8a43259ULL, 0x7e530b32cd264d43ULL, 0x5735c57eb5ada428ULL,
        0x8995bfb06650deb7ULL, 0xe4fb9d51dc6d178cULL, 0x92824d6cbdf56d00ULL, 0x61c7f65762965fd6ULL,
        0xfc26f846f58fc37bULL, 0xef5b7ee9e2919974ULL, 0x05c070ecd4965e75ULL, 0x09d8b5d10288814fULL,
        0x965b109c3e9b667cULL, 0x9f62af031043c845ULL, 0x7869265a9410ed64ULL, 0x8283e379e604b8e6ULL,
        0xb23608c0ff86848eULL, 0xa2b5ce1200cd95efULL, 0xdb7dc9381d7c5e01ULL, 0x7a3090fb9e93150aULL,
        0x9c8ac5d46c6b9adfULL, 0x43d58d1260985799ULL, 0xf873841b68d68d61ULL, 0x94b1273189153d31ULL,
        0xb0d308100f768ffbULL, 0x48a8f89d377a14f6ULL, 0x2f405fbc714385f3ULL, 0x168e9240e2c1d3d3ULL,
        0x585aea89b6e8d8a6ULL, 0x88a9966cd8271116ULL, 0x40d552745a49ce82ULL, 0xf37fdecfdaadc20bULL,
        0x48e8a7acc93d2052ULL, 0xe03e53773f6784beULL, 0x2b10d6bfc47d0fa8ULL, 0x2e9279c3f96db849ULL,
        0x3c76a8eecb62c406ULL, 0x0e111ddb5e0b3056ULL, 0xf2bde6fe7b1c8cdbULL, 0xc9d86ced65ea3f2bULL,
        0xe19e5cd2104120c4ULL, 0xcbd23c926616364fULL, 0x26b78ed9556c552aULL, 0x1d15e069d728b5b8ULL,
        0x5a02b6ab94264a27ULL, 0x614ed124292ece0eULL, 0x30e8c530e70a9eccULL, 0x4f333827070b10ebULL,
        0x14c8d3113d60847aULL, 0x533ffdde928680b5ULL, 0x6b4c15b68cdc660cULL, 0x9b17f90857a03a0dULL,
        0xd353db20ed87d40bULL, 0x4ea86dbe17ecd77dULL, 0x3e7407381227ea8dULL, 0x97048b8b16b4ea3aULL,
        0xaa9fe4b283cf5006ULL, 0x57feb699b24049f3ULL, 0xb7677782a5f6b211ULL, 0x34adce9d1de29a52ULL,
        0xf9c11a968225b928ULL, 0xfc04427ee69cd020ULL, 0x4b9626830798ef88ULL, 0x5821a4a4279e00b7ULL,
        0xf780b10dcc8c02dfULL, 0x5d8854ece2535549ULL, 0xd6d8908f8734c093ULL, 0x72fcbba29166fea6ULL,
        0xf6f7b9b7222dd308ULL, 0x540f5bad68f2e6e5ULL, 0xa9cb7c9f6a14244bULL, 0xc74b97e55f35eaa0ULL,
        0x9612f1c87f6b9af9ULL, 0x771842a5f3daffbeULL, 0x7d2d01aaf101c0c0ULL, 0x487ccb6487961e18ULL,
        0x6945c81050f8662dULL, 0xa386855a5bf0b5bbULL, 0xb57e869e9c23d0a9ULL, 0x0eeb1c6ad8f4fc77ULL,
        0x261894a6b9f4ec37ULL, 0xc44f8dd42d732e24ULL, 0xc41d318799dd0a6fULL, 0xe445d0a311ccfd80ULL,
        0x77a8f110ff127cb0ULL, 0x83139e43d6538ec1ULL, 0xb8e5e9496addde7eULL, 0x063bd4a7849218d9ULL,
        0xbcaec8bbae66273fULL, 0x4c8af8eef4b3c1ccULL, 0x117d25d2e87ae4f6ULL, 0xa0115fa8bfeb4c5fULL,
        0xb72b600fae244ea6ULL, 0x76294910eb61eb6bULL, 0x550fb7732436f236ULL, 0xfdbf10eb6d0c3ca7ULL,
        0x8b17d4edc9a674e1ULL, 0x3691cb693dfe79b7ULL, 0x7f99a3d715324ac8ULL, 0x6efe7401bc94f27cULL,
        0x4a7d4fe0de909a77ULL, 0x892cb92d14162d68ULL, 0x2fac81bb13637a95ULL, 0xfa50ee67e547dff4ULL,
        0x6c1a77a9f5796173ULL, 0xeae9d17f05ab766aULL, 0x5e6ccfe8757e4472ULL, 0xde26a90fd782e72dULL,
        0x940ca3ec71afa8b2ULL, 0x65b0222c84c388eeULL, 0x3456cfb039de4836ULL, 0x4cc29f896efd187cULL,
        0xb52ee0d230efe5e9ULL, 0x2bc79048c1042a98ULL, 0xc8bff570fa912f3fULL, 0xdf3a49ba012ebdc7ULL,
        0x905a5e6055e04b9fULL, 0xbab95172760730edULL, 0xd72b2b36f44258faULL, 0xe206a8e16bf93baeULL,
        0x6ccb64dce6ea4e61ULL, 0x37a00a7deab23582ULL, 0x9c1ec35935f9e31cULL, 0xc8c916fd45d4036eULL,
        0x2bd3ba4ba34c06baULL, 0x3cab321f6aeff597ULL, 0x2b158b564f453e32ULL, 0x9a2f13eb2cba821bULL,
        0x1de51b88c937f613ULL, 0xed228a0d19fdd03aULL, 0x9d5f685e4792e1bcULL, 0x4c02472ed9d3624fULL,
        0x2013e9040ec6a916ULL, 0x00a50d6974162cabULL, 0x308c5281448397e2ULL, 0x27e4ce4374160880ULL,
        0xad0a4d2c5cbc9920ULL, 0xfef9d42c3d72b8edULL, 0x5b259e7c7846a620ULL, 0x3023da10e4d22a42ULL,
        0xb85d96b7a2dfbf42ULL, 0xc20dda40a065c535ULL, 0xe473756099aac7bbULL, 0xe72ef067a37e6b2fULL,
        0x5d2ea29193078643ULL, 0x8ccb6e6b878a2e4eULL, 0xcaa7fbf37012d1f9ULL, 0x4146fa8468159e65ULL,
        0x15a0123907ac44fdULL, 0xd8fdbe3503e6cba8ULL, 0x85f1e552fa12591cULL, 0x47c4d77ad60f407bULL,
        0x0c59b72967809eceULL, 0xffdbad4aa727f94aULL, 0x1190078885c34beeULL, 0x11dd50ab805acbf5ULL,
        0x7f713a2781776563ULL, 0x0522de180c2bae20ULL, 0xda21fa756f67eceaULL, 0xb17c2b4827ff29f4ULL,
        0x459a733791ebdb4cULL, 0xe7c2fd534a1b958eULL, 0x859b8c615e78694cULL, 0xabd3f98614132e49ULL,
        0x3498ee281711c432ULL, 0xb4d90b4c0d7bd368ULL, 0x0f9202bd96bdc7ecULL, 0xceeb970e0fce62f8ULL,
        0x24827d36a44bc664ULL, 0xf6286fdff7b0747bULL, 0x7d006d8fed5303fbULL, 0x43b9420ff6058014ULL,
        0x00102f4ad64abd57ULL, 0xb067dca8c84b449bULL, 0xd5c6280664c0b604ULL, 0xa6d1764d6f91c965ULL,
        0xff253f6c8f09f043ULL, 0x3b711511fbc7abebULL, 0xb3e9f521a255b45bULL, 0x3f4c71fbd953b06eULL,
        0xa7e4adf57eea4681ULL, 0x993ac5f941ac6841ULL, 0x1d40417b2d0f7062ULL, 0x9ef7a23f3ce4395eULL,
        0x386e0e8dca27a67aULL, 0x3e2f09c61f882c34ULL, 0x61293206f713f676ULL, 0x403cdd6b17ff3d79ULL,
        0xb8b316f2f8654b39ULL, 0x98641bbc217b7ad0ULL, 0x69642327414dd4ebULL, 0x4e3a10f5306039baULL,
        0x0571bf9d572b442aULL, 0xa68b3200c3e8c42eULL, 0x2e3e91601b8f8749ULL, 0x5a2152852133e620ULL,
        0x87b15b6867091033ULL, 0x789ab733f23e9163ULL, 0x88f1debed648751bULL, 0xf086698c35e6c4a7ULL,
        0xc0c4da3a80a79cc0ULL, 0x48654e2eec240391ULL, 0xa346270aded07e3eULL, 0xd6673627d2d744e6ULL,
        0xff78a1e1adcaa85aULL, 0x03d0b608b46602c8ULL, 0x804dd071beb99759ULL, 0xea36caaca0574ca0ULL,
        0xaea46a662dcdafd4ULL, 0x386526058aa9ef7dULL, 0xce59dc2f580aeaffULL, 0x3bf7410989a38fbbULL,
        0x040540247505fc66ULL, 0x9576581f202dddb3ULL, 0x5dee5459d0e7d927ULL, 0x41c9aed3f6dd6178ULL,
        0x559d17b00d43f36fULL, 0x4e2200e0878d8476ULL, 0xc2212f831be73383ULL, 0xf894589937dd3c9cULL,
        0x57bbb8f6909dda15ULL, 0x3e5ce9539aded9cbULL, 0x010526485b19a6f0ULL, 0x1e93d18700fb9080ULL,
        0xf402c0aa5155be25ULL, 0xbcfc0380b2433cc7ULL, 0x4ff1bd4e68bb8f41ULL, 0xc62047dfacef7abbULL,
        0xa530d39c71d070aeULL, 0x0d52cd18607c3da6ULL, 0x2f52a190dd27690cULL, 0xde53697ff033ddf3ULL,
        0x0ea706cf040db922ULL, 0x59d8736bb3f1009bULL, 0x275446f42f15d8f8ULL, 0x870923f6b53c675cULL,
        0xc817a2db252eca94ULL, 0x457c3f9de6398d13ULL, 0x127ff027cab5a782ULL, 0xe34d202e323306e6ULL,
        0x9a4291f5fcfa302aULL, 0xe72349118a2019c9ULL, 0x95e06d12511bda29ULL, 0x1a5687f3beca1e98ULL,
        0x8fe1ce6de09d83a2ULL, 0xb0c58ae99f5bb795ULL, 0x57c684f23de14955ULL, 0x3e092d0663001aa1ULL,
        0x4309499d1befc908ULL, 0x1d7eae19ada79ec0ULL, 0xf98497cd4386f565ULL, 0x7176812059edd151ULL,
        0x3dcf48c0b6cbbb97ULL, 0x720a4ed040b4da2aULL, 0x489a95dc281b869aULL, 0xa56a26f3fe02b99bULL,
        0x8a2c014c44598e9bULL, 0xab8c9428a4efd303ULL, 0x358627ca23a1ea1aULL, 0xd7766a736511ac37ULL,
        0x0f9e310720d8d534ULL, 0x45b3c36c39e22cbeULL, 0x5fc646b918a73fa1ULL, 0xcdc0b2d9adb92436ULL,
        0xde94d35047d64ad9ULL, 0x0588f236e85f75fdULL, 0xbfc72c5f8dbbd61cULL, 0xaf2aa0f99587bbcfULL,
        0xa488bb07f8ce5c64ULL, 0x90c10b783b12f46dULL, 0x0a3fc1a4ea76c895ULL, 0xae993cab1f092f54ULL,
        0xf2170ead9ad3c0b4ULL, 0x4128e16c7b0c5e06ULL, 0x6d3174cac5879565ULL, 0xed13dde3a4a6a3caULL,
        0x0fda2fabbe42e26fULL, 0x715f3d682b832b0eULL, 0x04d8534c9d2e39d9ULL, 0x25d1e93141549afeULL,
        0x458ee5a96b5f2556ULL, 0x821835aa8da29c64ULL, 0xc027a193cbbef004ULL, 0xea9f8a0e3740dc5fULL,
        0x397885c9e63ed380ULL, 0x80796061e5ebbdccULL, 0x81fe96cd6d8dcbc5ULL, 0x6734eea8f8e9d65dULL,
        0x7fdd28da35aba08dULL, 0xfaa0e9140f997fdeULL, 0xc739a3228235900bULL, 0x1302d9b6f2fe7590ULL,
        0x0088b0996a4ac365ULL, 0x42b6bf8172680207ULL, 0x3ba442f930a44706ULL, 0x77b88625fe808798ULL,
        0xcc968e779395ae34ULL, 0x04d5db17a40cce84ULL, 0xc3dbfbd5badd8ecaULL, 0x8af60512a5dbdc6cULL,
        0xff3239faa9618e2dULL, 0x179fd9b4a29d88a0ULL, 0xfbad92c238485c90ULL, 0xf4897b836d0482b0ULL,
        0x9b8731e71924a75fULL, 0xb1ab878477e5510bULL, 0xd869e10063e589ccULL, 0x414b17cf1a1147b1ULL,
        0x5213de33e6a901f6ULL, 0x3114b5feee42dc11ULL, 0xcd645f1d868085c0ULL, 0x860b72bdf14e4664ULL,
        0x57334d705b855ab3ULL, 0x074c8e7308590cecULL, 0x5bb26cd5be8cae5cULL, 0xbdedd2469effdea5ULL,
        0x32d0687d79c0a5e0ULL, 0xb45ec7425b1fb340ULL, 0x62a4930c0c874078ULL, 0x0169350e6e64f771ULL,
        0xb1f3d72b9afcbd15ULL, 0x0e9d26744942dda3ULL, 0xe78c29b884ed0facULL, 0xca9d67b63486318bULL,
        0x3cd0db0fa9f16113ULL, 0x44a0142335b7befeULL, 0xee62f4284b60b5f2ULL, 0x66270bc33fd48f77ULL,
        0x91b8072ff99c6a46ULL, 0x9596c8a546fb729cULL, 0xef8623e5f59b1ab1ULL, 0x03057941f7dfe243ULL,
        0x9a1e3cb269ee424cULL, 0xba0ee35527f22b4dULL, 0xfaec405c5ebed26fULL, 0x2bca7b27d4a3ebe4ULL,
        0xfb3a0a124c2fbbedULL, 0xc33eb32ea3b30e6dULL, 0x009a7394aff6ce8cULL, 0xb708bfa6086d99bbULL,
        0x309261041cf87c16ULL, 0x5c27b049d36e1f94ULL, 0xa81ef4c6f2d3121aULL, 0x3f19f7f4864de8dbULL,
        0x5ac16ebb2c7e193aULL, 0x1515653a2ad90f88ULL, 0x5965e1bff9c5ec3cULL, 0x8180da2230f56709ULL,
        0xf41b78b2d15a928bULL, 0xedba491a0b5fdd2dULL, 0xcedb6c332cd8b24bULL, 0x61749b9c58f760fcULL,
        0x9f273b0e46687365ULL, 0x0383d7bfbbe8f8d2ULL, 0x397fc94eb6508be4ULL, 0xbc19c8e0dc79b286ULL,
        0x2aa40fc7d4d8b9c4ULL, 0x32d3e47528093a33ULL, 0x625d3f31df8bea9fULL, 0x813e0dc466f5ebc0ULL,
        0x1719e3e1cdbb88c2ULL, 0x7621ed62c17c194bULL, 0x2c6e135742a31c5aULL, 0xbb73b51c7775324bULL,
        0xc5e10079a25acd3dULL, 0x67e0fb6492f111eaULL, 0x97f2c6c068242ed4ULL, 0xf203201132e5e386ULL,
        0x2a328d8c75f4db6eULL, 0xf8bf57ee03c351c5ULL, 0xcb9a8df1b3285303ULL, 0xfc57e38528ed7ab4ULL,
        0x00c1116576417357ULL, 0x19842d6aa56f564fULL, 0xb91279ea06ac5ea4ULL, 0x1db5f4278273701cULL,
        0xd6b14062e3b3bbe5ULL, 0xd368e911981ca0a4ULL, 0xae13385bfcc57fa1ULL, 0xc9ff35021dea0a4dULL,
        0xae6dd3b6338aaa4eULL, 0x3537fef149f7e69bULL, 0x5439be52df189a52ULL, 0x2ab3b2f4f1ae31c6ULL,
        0x16ae9bd6b1aef9f4ULL, 0x56c63ac8e20268f6ULL, 0xafd8d55bdf744f8dULL, 0x385493ffafc186e9ULL,
        0x8a161de4d821cd84ULL, 0xefb6d1088b865296ULL, 0x379e3f85d2f46f00ULL, 0x18b21d6352c37349ULL,
        0x64b8d0ab881991fbULL, 0x74f2c9687c0783bbULL, 0x70e8b9c55db16356ULL, 0x6195842c98531f97ULL,
        0xc26288e1e918955aULL, 0xa59c6cdc28dc1b5aULL, 0x52f3ba06ea529ff6ULL, 0xa7453454b391c919ULL,
        0x2bb51a3160fbacc3ULL, 0x5dd28fdefc0cb301ULL, 0xfac932be68093132ULL, 0x2b3061b73ed88fc5ULL,
        0x44090452a929c406ULL, 0xfddb5d03c3d16d6bULL, 0x07d1a096e6440473ULL, 0x1b81f1c22bbdf2aeULL,
        0x187d9a174948aeb4ULL, 0x67682ac3f03c0d7aULL, 0x179bd2a1026ef7d4ULL, 0x6fa7083459fdc199ULL,
        0xdaa31e6c83d245bdULL, 0xfad7a5c21684a06bULL, 0xee454dd582489669ULL, 0x483e61d4872db6a2ULL,
        0x67cbf5bed37a7b2cULL, 0xf7b967101edbab0eULL, 0x24ddd32285ead2b3ULL, 0x1afb26166f44b6d9ULL,
        0xaf09917aca5f335eULL, 0xd4be2c1e1835ca79ULL, 0xc5e70768725ff053ULL, 0x35b5c6dada1846d9ULL,
        0x504a2bdc0fadf901ULL, 0x5e9c73650249ba1bULL, 0xcbcb1f8a4bba850bULL, 0x8f081533866bea39ULL,
        0x254129038d1df0f3ULL, 0xf7eb3b7a51c0a0f0ULL, 0x2e026981d1414edeULL, 0x4602de1f08b10704ULL,
        0x8943a19e6774a918ULL, 0x806cca2a7b198982ULL, 0x3d13f225da3e9758ULL, 0x3db40cee6768949fULL,
        0xb95e79346958d814ULL, 0xbaf46412e0299f7eULL, 0xff35df9d92e86288ULL, 0x6ef092a8da95d153ULL,
        0x0e64bba28ba36092ULL, 0x437e670ccfd23edeULL, 0x6a7f4851bbfda310ULL, 0xbd9d18020ea2b3eeULL,
        0x3bcaf797cb35c2bcULL, 0xf134ab39176de057ULL, 0xae7eec4181fcac71ULL, 0x2d2d9c9293cc2f9eULL,
        0xc75e0d6411b7ca40ULL, 0xccda382153005dbfULL, 0x8bf6866d57a6b732ULL, 0x10c5a1cbec8acb28ULL,
    }};

    inline static constexpr std::array<Hash, 2> kTurnHashes{{
        0x63e3463daa72f424ULL, 0xc7f6d5d9cfc83ffeULL,
    }};

    inline static constexpr std::array<Hash, 2> kMoveLeftHashes{{
        0x646319b741ca5cf4ULL, 0x3f00c185c742fce3ULL,
    }};

    inline static constexpr std::array<Hash, 16> kCastlingRightsHashes{{
        0xb12684149ec3a634ULL, 0x767296d1da5353b8ULL, 0x7e4f84786411a160ULL, 0x0bf283beb3dbab10ULL,
        0xa313b7f2f717a315ULL, 0x096a072794995293ULL, 0x1b80457bacb0e4baULL, 0x7645ff75ff9d5dc1ULL,
        0xfedcf28fd30962bcULL, 0xe9a1dadb4d50fb44ULL, 0x4e618224c5ad5616ULL, 0xfafda59e7fa1fa33ULL,
        0xec3c85d68700fa13ULL, 0x2955eec9c0b29530ULL, 0x71b4a4437d234606ULL, 0x8ce4154a814e4e01ULL,
    }};

    struct RollbackState {
        Move move;
        int moved_piece = 0;
        int captured_piece = 0;
        int old_turn = 0;
        int old_move_left = 0;
        int old_game_status = 0;
        std::uint8_t old_castling_rights = 0;
        Move old_last_move;
        Hash old_hash = 0;
        std::uint64_t old_halfmove_clock = 0;
        std::uint64_t old_fullmove_number = 1;
    };

    std::vector<std::vector<int>> current_board{8, std::vector<int> (8, 0)};
    std::vector<RollbackState> rollback;
    std::unordered_map<int, Hash> piece_hash, turn_hash, move_left_hash;
    std::unordered_map<int, Hash> castling_rights_hash;
    std::unordered_map<int, std::vector<std::pair<int, int>>> move_pattern;
    std::unordered_map<int, std::vector<std::pair<int, int>>> pawn_move_pattern;
    std::unordered_map<int, std::vector<std::pair<int, int>>> pawn_capture_pattern;
    int turn;
    int move_left;
    int game_status;
    std::uint64_t halfmove_clock = 0;
    std::uint64_t fullmove_number = 1;
    std::uint8_t castling_rights;
    Hash board_hash;
    Move last_move;

    bool valid_move_geometry(int old_x, int old_y, int new_x, int new_y,
                             SpecialMove* special_move = nullptr) const;
    bool valid_castle(int old_x, int old_y, int new_x, int new_y,
                      SpecialMove& special_move) const;
    void update_castling_rights(int old_x, int old_y, int new_x, int new_y,
                                int moved_piece, int captured_piece);
    Hash calculate_hash() const;
    void refresh_hash();
    void set_piece_with_hash(int row, int column, int piece);

public:
    Board();
    ~Board() = default;
    std::string export_fen() const;
    bool import_fen(const std::string& fen);

    auto begin() -> std::vector<std::vector<int>>::iterator;
    auto end() -> std::vector<std::vector<int>>::iterator;
    auto cbegin() const -> std::vector<std::vector<int>>::const_iterator;
    auto cend() const -> std::vector<std::vector<int>>::const_iterator;

    bool inboard(int x, int y) const;
    bool check_game_ended();
    bool has_game_ended() const;
    int get_current_turn() const;
    int get_moves_left() const;
    int get_game_status() const;
    int get_piece(int x, int y) const;
    Hash get_hash() const;
    std::uint8_t get_castling_rights() const;
    const Move& get_last_move() const;
    std::vector<Move> legal_moves();
    void get_turn();
    bool valid_move(int old_x, int old_y, int new_x, int new_y);
    bool make_move(const Move& move);
    bool make_move(int old_x, int old_y, int new_x, int new_y);
    bool make_capture(int old_x, int old_y, int new_x, int new_y);
    void rollback_move();
};


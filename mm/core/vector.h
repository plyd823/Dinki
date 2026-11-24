//#pragma once
//
//class CVector4f {
//    union {
//        struct {
//            float x;
//            float y;
//            float z;
//            float w;
//        };
//        float v[4];
//    };
//};
//
//class CVector3f {
//    public:
//    union {
//        struct {
//            float x;
//            float y;
//            float z;
//        };
//        float v[3];
//    };
//
//    static const CVector3f UP;
//    static const CVector3f ZERO;
//
//    public:
//    CVector3f(float, float, float);
//    CVector3f();
//};
//
//class CVector2f {
//    public:
//    union {
//        struct {
//            public:
//            float x;
//            float y;
//        };
//        
//        public:
//        float v[2];
//    };
//
//    public:
//    CVector2f(float, float);
//};
#include "test.h"
#include "taelia/sim/world.h"
#include <cmath>
using namespace taelia;

namespace {
WorldDesc contactWorld() {
    WorldDesc d; d.magic.levels=1; d.magic.columns=16; d.magic.layers[0]=16; d.magic.threads=1;
    return d;
}
void ticks(World& w,int count) { for(int i=0;i<count;++i) w.step(); }
void groundShot(World& w,Aspect aspect,double kg,double height=0.05) {
    const dvec3 p{0,w.groundHeight(0,0),0};
    w.setFocus(p+dvec3{0,2,0}); w.step();
    w.throwMagicBall(p+dvec3{0,height,0},{0,-10,0},aspect,kg);
}
}

TEST(impact_threshold_uses_remaining_mass_and_has_no_weak_visual_or_ignition) {
    for(double kg : {29.99,30.0,30.01}) {
        World w(contactWorld());
        groundShot(w,Aspect::Fire,kg);
        w.step();
        CHECK(w.magicBalls().empty());
        CHECK(w.magicImpacts().empty()==(kg<30));
        CHECK(w.scorches().empty()==(kg<30));
        if(kg<30) { ticks(w,90); CHECK(w.scorches().empty()); CHECK(w.burningTrees().empty()); }
        else { CHECK(w.magicImpacts().front().aspect==Aspect::Fire); ticks(w,60); CHECK(w.magicImpacts().empty()); }
    }
    for(double kg : {30.1,30.6}) {
        World w(contactWorld());
        groundShot(w,Aspect::Fire,kg,0.5);
        ticks(w,5);
        CHECK(w.magicBalls().empty());
        CHECK(w.magicImpacts().empty()==(kg<30.5)); // distance loss crosses 30 before contact
    }
}

TEST(weak_earth_and_necrotic_impacts_do_not_damage_ground_or_grass) {
    for(Aspect aspect : {Aspect::Earth,Aspect::Necrotic}) {
        World w(contactWorld());
        groundShot(w,aspect,29.9);
        const auto edits=w.terrain().editsVersion();
        ticks(w,90);
        CHECK(w.magicImpacts().empty());
        CHECK(w.terrain().editsVersion()==edits);
        CHECK(w.magicAt({0,w.groundHeight(0,0)+0.5,0}).grassDamage<1e-6);
    }
}

TEST(cold_campfire_can_be_relit_but_wind_or_weak_fire_cannot_ignite_it) {
    World w(contactWorld());
    const dvec3 core=w.campfireCore();
    w.setFocus(core); ticks(w,2);
    const dvec3 hit=core+dvec3{0,-0.39,0};
    w.throwMagicBall(hit,{0,-10,0},Aspect::Water,100); w.step();
    CHECK(w.campfireStrength()<0.01);
    // Remove the quenching water before testing fresh ignition, as on a dry bed.
    MagicDensities d; double volume;
    for(int z=-2;z<=2;++z) for(int x=-2;x<=2;++x) for(int y=0;y<3;++y) {
        const dvec3 p=core+dvec3{2.0*x,2.0*y,2.0*z};
        if(w.magic().cellAt(p,d,&volume)) w.magic().addMass(p,Aspect::Water,-d[Aspect::Water]*volume);
    }
    w.throwMagicBall(hit,{0,-10,0},Aspect::Air,60); w.step();
    CHECK(w.campfireStrength()<0.01);
    w.throwMagicBall(hit,{0,-10,0},Aspect::Fire,29); w.step();
    CHECK(w.campfireStrength()<0.01);
    w.throwMagicBall(hit,{0,-10,0},Aspect::Fire,60); w.step();
    CHECK(w.campfireStrength()>0.1);
    ticks(w,30);
    CHECK(w.campfireStrength()>0.1);
}

TEST(crown_ignition_stays_at_the_contact_and_life_restores_the_burnt_tree) {
    World w(contactWorld());
    std::vector<TreeInstance> trees;
    treesInArea(w.terrain(),-40,-40,40,40,trees);
    CHECK(!trees.empty()); if(trees.empty()) return;
    const TreeInstance tree=trees.front();
    const double height=growTree(tree.kind,treeSkeletonSeed(tree.kind,tree.variant)).height*tree.scale;
    const dvec3 crown=tree.position+dvec3{0.5,height*0.75,0};
    w.setFocus(crown); ticks(w,2);
    CHECK(w.magic().addDensity(crown,Aspect::Fire,90)); ticks(w,4);
    bool lit=false;
    for(const auto& b:w.burningTrees()) if(length(b.position-tree.position)<0.1 && b.strength>0.1) {
        lit=true; CHECK(b.firePosition.y>tree.position.y+height*0.4);
    }
    CHECK(lit);
    const auto key=std::make_pair(int64_t(std::floor(tree.position.x/kTreeCell)),int64_t(std::floor(tree.position.z/kTreeCell)));
    w.clock().jumpDays(1); w.step();
    CHECK(w.burntTrees().count(key)==1u);
    // Cold after the burn; use a real packet collision with the standing trunk.
    const dvec3 trunk=tree.position+dvec3{0,0.6,0};
    w.throwMagicBall(trunk,{0,0,1},Aspect::Life,29); ticks(w,20);
    CHECK(w.burntTrees().count(key)==1u);
    w.throwMagicBall(trunk,{0,0,1},Aspect::Life,60); ticks(w,30);
    bool recovering=false;
    for(const auto& b:w.burningTrees()) if(length(b.position-tree.position)<0.1 && b.strength==0)
        recovering=b.burnt>0 && b.burnt<1;
    CHECK(recovering);
    ticks(w,300);
    CHECK(w.burntTrees().count(key)==0u);
}

TEST(runes_regulate_to_half_without_creating_energy_or_overshooting) {
    MagicOptions opt; opt.levels=1; opt.columns=16; opt.layers[0]=8; opt.threads=1;
    opt.flatGround=true; opt.takingRule=false;
    MagicField field(nullptr,nullptr,opt); const dvec3 p{1,3,1}; field.setFocus(p); field.step(0);
    MagicDensities d; double volume; CHECK(field.cellAt(p,d,&volume));
    for(int a=0;a<kAspects;++a) field.addMass(p,Aspect(a),-d[Aspect(a)]*volume);
    Rune r; r.position=p; r.normal={0,0,1}; r.placement=RunePlacement::Air; r.aspect=Aspect::Fire;
    field.addMass(p,Aspect::Water,20);
    stepRune(r,field,120); CHECK_NEAR(r.mass(),0,1e-9);
    field.addMass(p,Aspect::Neutral,2);
    stepRune(r,field,120); CHECK_NEAR(r.mass(),2,1e-6); // cannot take unavailable energy
    field.addMass(p,Aspect::Fire,30);
    const double before=field.totalMass(Aspect::Fire)+field.totalMass(Aspect::Neutral)+r.mass();
    for(int i=0;i<20;++i) stepRune(r,field,30);
    CHECK_NEAR(r.density(),75,1e-5);
    CHECK_NEAR(field.totalMass(Aspect::Fire)+field.totalMass(Aspect::Neutral)+r.mass(),before,1e-4);
    r.contents[int(Aspect::Fire)]=100*r.volume();
    const double over=field.totalMass(Aspect::Fire)+r.mass();
    for(int i=0;i<20;++i) { stepRune(r,field,30); CHECK(r.density()>=75-1e-9); }
    CHECK_NEAR(r.density(),75,1e-5);
    CHECK_NEAR(field.totalMass(Aspect::Fire)+r.mass(),over,1e-4);
}

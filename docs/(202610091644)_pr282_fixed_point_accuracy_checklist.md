# PR281 follow-up accuracy checklist

Local timestamp: 2026-10-09T16:44:24.122302+09:00.

Baseline main3dace088a6ce867df70865dda8e1b6d01cdff2e1 has the same source tree as PR281a289. Original dirty worktree is untouched. The closed small-RHS, physical H transpose and operand FD-floor findings stay closed.

- [x] Pin fine16×12×8 returned state8480entries,105physical observations at150/300s,sigma3e-4,baselineh5/h10andcontext. FixtureZIP SHA0868d63512a6be28ad1ba8616e848cab3c683bd76297ecf23c17bcf93f9f791f.
- [x] Add only explicit test-CLI schedule120×2.5; existing schedules/config/production equations unchanged. CPP SHA5474b4c1974408b201cd77465128ee7825d696d058750850018e0dc204e3aada. Invalid120×2 rejected before grid/output; localtargetbuildpass.
- [x] Inspect/refresh Graphify and manualruntimecallers. Corpus342nodes877edges; runtimeCLI/contextcontentsareextractiongaps;graphisnotnumericalproof.
- [x] Green/Red first-probe review: descriptorcontext+exactfreshbasisguards;onefullVJP at120×2.5; nooptimization/datachange. Maccontextmismatchpreserved,physicaltrajectorynotrunonMac.
- [ ] Linuxfixed-initialstateh2.5evaluation; compareper-timepredictionRMS,costcross+quadratic,gradientandbracketsagainsth5/h10.
- [ ] IsolategradientuncertaintyupstreamofobservationH. Existingdirectional150/300termsabout+1.82569/−1.82569cancel~1e6;totalFDalonecannotcertifyaccuracy. PertimeVJP/model-responsecomparisonisnextonlyaftertimeprobe.
- [ ] Establishwholefour-controlgradientaccuracybudget beforecertifiedtermination; oneDDorKrylovreplaycannotboundEg.
- [ ] Reoptimizeonlyinanerror-separatedsettingifrequired. Do notclaimweatherstate/forecastrecoveryfromcostalone.
- [ ] FinalGreen/RedreviewandappropriateaffectedCI;no merge.

Predeclared diagnostics: per-timeRMSchangecomparedto0.1sigma(engineeringtarget,notvalidatederrorbound); raw-gradientgate robustness requiresbothg2.5norm<1e-5 andg2.5−g5norm<4.5434314e-6. This singlehalvingdoesnotproveorder,continuumaccuracyorEg. The source data,control andR remainfixed;costdifferenceisdecomposedwithresidualincrements. No higherHessianengine,newoptimizationframework,productiontolerance/delta tuningorcheckpointframework.

PriorqualifiedWRF48stage/byteidentity/RK3comparisonatPR281isretained;thischangeis test-CLI/scheduling only and no newWRFmodelrun orRK3comparison has been performed. ProductionC++/Fortransourcesremainunchanged.

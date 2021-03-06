/*
 *  DCDCBoostEvalOp.cpp
 *  Copyright 2010 Jean-Francois Dupuis.
 *
 *  This file is part of HBGGP.
 *  
 *  HBGGP is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  HBGGP is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with HBGGP.  If not, see <http://www.gnu.org/licenses/>.
 *  
 *  This file was created by Jean-Francois Dupuis on 25/03/10.
 */

#include "DCDCBoostEvalOp.h"

#include "BGFitness.h"
#include <BondGraph.h>
#include <RootReturn.h>
#include "BGContext.h"
#include "GrowingHybridBondGraph.h"
#include "TreeSTag.h"
#include <cfloat>
#include "DCDCBoostLookaheadController.h"
#include "BGException.h"
#include "IndividualPlotter.h"
#include "ParametersHolder.h"
#include "VectorUtil.h"

using namespace Beagle;
using namespace BG;

#ifdef SINGLE_OUTPUT
#define NBOUTPUTS 1
#define NBPARAMETERS 2
#else
#define NBOUTPUTS 3
#define NBPARAMETERS 3
#endif

//#define NOSIMULATION

DCDCBoostEvalOp::DCDCBoostEvalOp(std::string inName) : BondGraphEvalOp(inName)
{ 
	mIndividualCounter = 0;
}

DCDCBoostEvalOp::~DCDCBoostEvalOp() {
}

/*!
 *  \brief Evaluate the fitness of the given individual.
 *  \param inIndividual Current individual to evaluate.
 *  \param ioContext Evolutionary context.
 *  \return Handle to the fitness value of the individual.
 */
Beagle::Fitness::Handle DCDCBoostEvalOp::evaluate(Beagle::GP::Individual& inIndividual, Beagle::GP::Context& ioContext) {
	Beagle_StackTraceBeginM();
	Beagle_AssertM(inIndividual.size() == 1);
	
	
	BGFitness *lFitness = new BGFitness(-1);
	GrowingHybridBondGraph::Handle lBondGraph;
	TreeSTag::Handle lTree = castHandleT<TreeSTag>(inIndividual[0]);
	
	
	
	try {
		//Get the parameters
		Beagle::Component::Handle lHolderComponent = ioContext.getSystem().getComponent("ParametersHolder");
		if(lHolderComponent==NULL)
			throw Beagle_RunTimeExceptionM("No ParameterHolder holder component found in the system!");
		ParametersHolder::Handle lHolder = castHandleT<ParametersHolder>(lHolderComponent);
		if(lHolder==NULL)
			throw Beagle_RunTimeExceptionM("Component named \"ParameterHolder\" found is not of the good type!");
		lHolder->clear();
		
		//Run the individual to create the bond graph.
		RootReturn lResult;
		inIndividual.run(lResult, ioContext);
		BGContext& lContext = castObjectT<BGContext&>(ioContext);
		lBondGraph = castHandleT<GrowingHybridBondGraph>(lContext.getBondGraph());
		
		Beagle_LogDebugM(
						 ioContext.getSystem().getLogger(),
						 "evaluation", "DCDCBoostEvalOp",
						 std::string("Evaluating bondgrap: ")+
						 lBondGraph->BondGraph::serialize()
						 );
		
		GrowingHybridBondGraph::Handle lSaveNonSimplifiedBondGraph = new GrowingHybridBondGraph;
		*lSaveNonSimplifiedBondGraph = *lBondGraph;
		lFitness->setBondGraph(lSaveNonSimplifiedBondGraph);	

		
		
#ifdef DEBUG
		ofstream lFilestream("Bondgraph_ns.xml");
		PACC::XML::Streamer lStreamer(lFilestream);
		lBondGraph->write(lStreamer);
		inIndividual.write(lStreamer);
		lBondGraph->plotGraph("BondGraph_ns.svg");
#endif
		lBondGraph->simplify();	
		lFitness->setSimplifiedBondGraph(lBondGraph);
		
		/*///////////////////
		if( ioContext.getSystem().getRandomizer().rollUniform(0,0.999) > 0.9 )
			lFitness->setValue(0);
		else
			lFitness->setValue(ioContext.getSystem().getRandomizer().rollUniform(0,0.999));
		return lFitness;
		/*//////////////////
		
		
		Beagle_LogDebugM(
						 ioContext.getSystem().getLogger(),
						 "evaluation", "DCDCBoostEvalOp",
						 std::string("Evaluating simplified bondgrap: ")+
						 lBondGraph->BondGraph::serialize()
						 );
#ifdef DEBUG
		lBondGraph->plotGraph("BondGraph.svg");
		ofstream lFilestream2("Bondgraph.xml");
		PACC::XML::Streamer lStreamer2(lFilestream2);
		lBondGraph->write(lStreamer2);
		inIndividual.write(lStreamer2);
		plotIndividual(inIndividual,"Individual.svg");
#endif
	
		//Check if the restriction of the number of switch is fullfilled
		if( (lBondGraph->getSwitches().size() > mMaxNumberSwitch->getWrappedValue()) && (mMaxNumberSwitch->getWrappedValue() != -1) ) {
			lFitness->setValue(0);
			return lFitness;
		}
		
		//Evaluate the bond graph
		//Initialize the simulation
		std::map<std::string, std::vector<double> > &lLogger = lBondGraph->getSimulationLog();
		DCDCBoostLookaheadController *lController = dynamic_cast<DCDCBoostLookaheadController*>(lBondGraph->getControllers()[0]);
		lController->setSimulationDuration(mContinuousTimeStep->getWrappedValue());
		
		if(mAllowDifferentialCausality->getWrappedValue() <= 1) {
			lBondGraph->setDifferentialCausalitySupport(false);
		}
		
		try {
			std::vector<double> lInitialOutput(1,0);
		
			//Simulate every target prior to this generation
			vector<double> lFitnessVector;
			unsigned int lTry = 0;
			for(int g = mSimulationCases.size()-1; g >= 0; --g) {
				bool lRun = false;
				if( (*mGenerationSteps)[0] < 0 )
					lRun = true;
				else if( ioContext.getGeneration() >= (*mGenerationSteps)[g] )
					lRun = true;
				if(lRun) {
					
					double lF = 0;
					bool lSimulationRan = false;
										
					for(unsigned int i = 0; i < mSimulationCases[g].getSize(); ++i) {
						
						if( mSimulationCases[g].getTime(i) >= mSimulationDuration->getWrappedValue() )
							throw Beagle_RunTimeExceptionM("DCDCBoostEvalOp : Applying control target later than simulation end");
						if(mSimulationCases[g].getTargets(i).size() != NBOUTPUTS-1)
							throw Beagle_RunTimeExceptionM("DCDCBoostEvalOp : There should be 1 target value for each control time");
						if(mSimulationCases[g].getParameters(i).size() != NBPARAMETERS)
							throw Beagle_RunTimeExceptionM("DCDCBoostEvalOp : There should be 1 parameter value for each control time");
		
						//Assign parameters
						const vector<double>& lParameters = mSimulationCases[g].getParameters(i);
						if(!lParameters.empty()) {
							lBondGraph->clearStateMatrix();
						}
						assert(lHolder->size() == lParameters.size());
						for(unsigned int k = 0; k < lParameters.size(); ++k) {
							(*lHolder)[k]->setValue(lParameters[k]);
						}
						mSourceValue = lParameters[0];
						
						//Compute the current target
						vector<double> lTargets = mSimulationCases[g].getTargets(i);
						double lCurrentTarget = 0;
						for(unsigned int k = 0; k < lTargets.size(); ++k) {
							lCurrentTarget += lTargets[k]*lTargets[k]/(lParameters[0]*lParameters[k+1]);
						}
						lTargets.push_back(lCurrentTarget);
						lController->setTarget(lTargets);

						if(i == 0) {
							//Find a valid initial state
							unsigned int lInitialSwitchState = 0;
							double lMaxConfiguration = pow(2.0,int(lBondGraph->getSwitches().size()));
							for(;lInitialSwitchState < lMaxConfiguration; ++lInitialSwitchState) { 
								try{ 
									lController->initialize(&(*lBondGraph),lInitialSwitchState);
								} catch(BG::CausalityException inError) {
									continue;
								}
								break;
							}
														
							//Check if a valid initial state have been found
							if(lInitialSwitchState == lMaxConfiguration) {
								if(mAllowDifferentialCausality->getWrappedValue() == 2) {
									lBondGraph->setDifferentialCausalitySupport(true);
									lInitialSwitchState = 0;
								} else {
									lSimulationRan = false;
									throw BG::CausalityException("No initial state found!");
									break;
								}
							}
							
							//Reset the bond graph
							lLogger.clear();
							lBondGraph->reset();
							
//							//Remove transition state when test hand writen individual
//							vector<double> lInitialState(3,0);
//							lInitialState[0] = 150e-6*lCurrentTarget;
//							lInitialState[1] = 800e-6*lTargets[0];
//							lInitialState[2] = 146.6e-6*lTargets[1];
//							lBondGraph->setInitialStateVariable(lInitialState);
						}
						
#ifndef NOSIMULATION
						//Run the simulation
						lSimulationRan = true;
						if(i < mSimulationCases[g].getSize()-1) 
							lBondGraph->simulate(mSimulationCases[g].getTime(i+1),mContinuousTimeStep->getWrappedValue(),false);
						else
							lBondGraph->simulate(mSimulationDuration->getWrappedValue(),mContinuousTimeStep->getWrappedValue(),false);
#endif
							
					}
					
					//Evaluate the results
					if(lSimulationRan) {
						lF = computeError(&(*lBondGraph),lLogger);

						
						
						//Clean logger
						for(map<std::string, vector<double> >::iterator lIter = lLogger.begin(); lIter != lLogger.end();) {
							if(lIter->first != "Output_0" &&
							   lIter->first != "Output_1" &&
							   lIter->first != "Output_2" &&
							   lIter->first != "Target_0" &&
							   lIter->first != "Target_1" &&
							   lIter->first != "Target_2" &&
							   lIter->first != "State" &&
							   lIter->first != "time" &&
							   lIter->first != "S1") {
								lLogger.erase(lIter++);
							} else {
								++lIter;		
							}
						}
						lBondGraph->writeSimulationLog(std::string("DCDCBoost_Lookahead_testcase_")+uint2str(g)+std::string(".csv"));
					} else {
						lF = 0;
					}
				
					//Log simulation data
					lFitnessVector.push_back(lF);
					lFitness->addDataSet(lTry, lF);
					for(map<std::string, std::vector<double> >::const_iterator lIter = lLogger.begin(); lIter != lLogger.end(); ++lIter) {
						lFitness->addData(lIter->first, lIter->second,lTry);
					}
					++lTry;
				}
			}
			
			//Take the average of all test case
			double lAvg = 0;
			for(unsigned int i = 0; i < lFitnessVector.size(); ++i) {
				lAvg += lFitnessVector[i];
			}
			lAvg = lAvg/lFitnessVector.size();
			lFitness->setValue(lAvg);
			
			/*	
			 //Look at the worst test case
			 double lMinF = DBL_MAX;
			 for(unsigned int i = 0; i < lFitnessVector.size(); ++i) {
			 if(lFitnessVector[i] < lMinF) {
			 lMinF = lFitnessVector[i];
			 }
			 }
			 
			 lFitness->setValue(lMinF);
			 */		
//			Beagle_LogDebugM(
//							 ioContext.getSystem().getLogger(),
//							 "evaluation", "DCDCBoostEvalOp",
//							 std::string("Result of evaluation: ")+
//							 lFitness->serialize()
//							 );
			
		} 
		catch(BG::CausalityException inError) {
			lFitness->setValue(0);
		}
		
	}
	catch(std::runtime_error inError) {
		std::cerr << "Error catched while evaluating the bond graph: " << inError.what() << std::endl;
		
		//Save bond graph for debuging
		std::ostringstream lFilename;
		lFilename << "bug/bondgraph_bug_" << ioContext.getGeneration() << "_" << mIndividualCounter++;//ioContext.getIndividualIndex();
#ifndef WITHOUT_GRAPHVIZ
		lBondGraph->plotGraph(lFilename.str()+std::string(".svg"));
#endif
		ofstream lFileStream((lFilename.str()+std::string(".xml")).c_str());
		PACC::XML::Streamer lStreamer(lFileStream);
		lBondGraph->write(lStreamer);
		inIndividual.write(lStreamer);
		lFileStream << endl;	
		//Assign null fitness
		lFitness->setValue(0);
		
#ifdef STOP_ON_ERROR
		exit(EXIT_FAILURE);
#endif
    }
	
	
	
	//delete lBondGraph;
	
#ifdef NOSIMULATION
	lFitness->setValue(ioContext.getSystem().getRandomizer().rollUniform(0,0.999));
#endif
	Beagle_LogDebugM(
					 ioContext.getSystem().getLogger(),
					 "evaluation", "DCDCBoostEvalOp",
					 std::string("Result of evaluation: ")+
					 lFitness->serialize()
					 );
	
	Beagle_LogTraceM(
					 ioContext.getSystem().getLogger(),
					 "evaluation", "DCDCBoostEvalOp",
					 std::string("Result of evaluation: ")+
					 dbl2str(lFitness->getValue())
					 );
	
	return lFitness;
	
	Beagle_StackTraceEndM("void DCDCBoostEvalOp::evaluate(Beagle::GP::Individual& inIndividual, Beagle::GP::Context& ioContext)");
}

double DCDCBoostEvalOp::computeError(const BondGraph* inBondGraph, std::map<std::string, std::vector<double> > &inSimulationLog) {
	
	std::vector<double> lErrors(NBOUTPUTS,0);
	std::vector<bool> lZeroOutput(NBOUTPUTS,true);
	std::vector<bool> lSourceOutput(NBOUTPUTS,true);
	bool lSameOutput = true;
	for(unsigned int k = 0; k < NBOUTPUTS; ++k) {
		const std::vector<double>& lTarget = inSimulationLog[std::string("Target_")+int2str(k)];
		std::vector<double>& lOutput = inSimulationLog[std::string("Output_")+int2str(k)];
		const std::vector<double>& lTime = inSimulationLog["time"];
		
		unsigned int lDataSize = lTime.size();
		assert(lTime.size() == lOutput.size());
		assert(lTime.size() == lTarget.size());
		assert(lDataSize != 0);
		
		std::vector<double> lError(lDataSize);
		for(unsigned int i = 0; i < lDataSize; ++i) {
			lError[i] = fabs(lOutput[i] - lTarget[i])/lTarget[i];// * lTime[i]/lTime.back()*2;
			
			if(lOutput[i] != 0) 
				lZeroOutput[k] = false;
			
			if(lOutput[i] != mSourceValue) 
				lSourceOutput[k] = false;
			
			if(lSameOutput && k == 0) {
				if(inSimulationLog[std::string("Output_")+int2str(k)][i] != inSimulationLog[std::string("Output_")+int2str(k+1)][i]) 
					lSameOutput = false;
			}
/*
			if( k < 2 ) 
				lError[i] = fabs(lOutput[i] - lTarget[i])/lTarget[i];	
			else {
				//Compute the error on the current by making sure it doesn't exceed a limit
				double lCurrentLimit = 2;
				lError[i] = (lOutput[i] - lCurrentLimit)/lCurrentLimit;
				if( lError[i] < 0 )
					lError[i] = 0;
				else
					lError[i] = fabs(lError[i] * 10); //If over the limits, greatly penalise
			}
*/
		}
		
		//Integrate the error vector
		for(unsigned int i = 0; i < lDataSize-1; ++i) {
			double dt = lTime[i+1] - lTime[i];
			lErrors[k] += (lError[i]+lError[i+1])/2*dt;
		}
	}

	
//	//Take the mean errors of the two targets
//	double lF = 0;
//	for(unsigned int k = 0; k < lErrors.size(); ++k) {
//		lF += lErrors[k];
//	}
//	lF = lF/lErrors.size();
	
	//cout << "Errors: " << lErrors << endl;
	
	//Take the worst errors
	double lF = lErrors[0];
	for(unsigned int k = 1; k < lErrors.size(); ++k) {
		lF = max(lF,lErrors[k]);
	}
	
//	//Look at the worst of the output voltage, disregard the current
//	double lF = max(lErrors[0],lErrors[1]);
		

	
	if(lF != 0)
		lF = 1/lF;
	else
		lF = DBL_MAX;
	
	//Look if the output is null
	bool lZero = true;
	for(unsigned int i = 0 ; i < lZeroOutput.size(); ++i) {
		lZero = lZero && lZeroOutput[i];
		if(!lZero)
			break;
	}
	if(lZero)
		lF = 0;


	//Look if the output is the same as the source
	if(lF != 0) {
		for(unsigned int i = 0 ; i < NBOUTPUTS; ++i) {
			if(lSourceOutput[i] || lSameOutput) {
				lF = mPenaltyFactor->getWrappedValue()*lF;
				break;
			}
		}
	}
	
	return lF;		
}



void DCDCBoostEvalOp::initialize(Beagle::System& ioSystem) {
#ifdef USE_MPI
	Beagle::MPI::EvaluationOp::initialize(ioSystem);
#else
	Beagle::EvaluationOp::initialize(ioSystem);
#endif
	
	PACC::XML::Streamer lStreamer(std::cout);
	ioSystem.getRegister().write(lStreamer,true);
	
	if(ioSystem.getRegister().isRegistered("bg.allow.diffcausality")) {
		mAllowDifferentialCausality = castHandleT<Int>(ioSystem.getRegister()["bg.allow.diffcausality"]);
	} else {
		mAllowDifferentialCausality = new Int(0);
		Register::Description lDescription(
										   "Allow differential causality in the bond graph",
										   "Int",
										   mAllowDifferentialCausality->serialize(),
										   "Allow differential causality in the bond graph, 0: No causality allowed, 1: Only if no configuration whithout diff causality, 2: All causality allowed."
										   );
		ioSystem.getRegister().addEntry("bg.allow.diffcausality", mAllowDifferentialCausality, lDescription);
	}
	
	if(ioSystem.getRegister().isRegistered("sim.control.generationstep")) {
		mGenerationSteps = castHandleT<FloatArray>(ioSystem.getRegister()["sim.control.generationstep"]);
	} else {
		mGenerationSteps = new FloatArray(1,-1);
		Register::Description lDescription(
										   "Generation for control target transition, -1 mean no transition",
										   "FloatArray",
										   mGenerationSteps->serialize(),
										   "Generation for control target transition"
										   );
		ioSystem.getRegister().addEntry("sim.control.generationstep", mGenerationSteps, lDescription);
	}
	
	if(ioSystem.getRegister().isRegistered("sim.control.target")) {
		mTargetString = castHandleT<String>(ioSystem.getRegister()["sim.control.target"]);
	} else {
		mTargetString = new String;
		Register::Description lDescription(
										   "Controller target, by pair for each control time",
										   "String",
										   mTargetString->serialize(),
										   "Tank levels target for the controller"
										   );
		ioSystem.getRegister().addEntry("sim.control.target", mTargetString, lDescription);
	}
	
	if(ioSystem.getRegister().isRegistered("bg.max.switch")) {
		mMaxNumberSwitch = castHandleT<Int>(ioSystem.getRegister()["bg.max.switch"]);
	} else {
		mMaxNumberSwitch = new Int(-1);
		Register::Description lDescription(
										   "Maximum number of switch allowed in the bond graph, -1 is unlimited",
										   "Int",
										   mMaxNumberSwitch->serialize(),
										   "Maximum number of switch"
										   );
		ioSystem.getRegister().addEntry("bg.max.switch", mMaxNumberSwitch, lDescription);
	}
	
//	if(ioSystem.getRegister().isRegistered("sim.dynamic.levelini")) {
//		mTanksLevelsIni = castHandleT<FloatArray>(ioSystem.getRegister()["sim.tanks.levelini"]);
//	} else {
//		mTanksLevelsIni = new FloatArray(3,0.25);
//		Register::Description lDescription(
//										   "Initial tank levels",
//										   "Float",
//										   mTanksLevelsIni->serialize(),
//										   "Initial tank levels"
//										   );
//		ioSystem.getRegister().addEntry("sim.dynamic.levelini", mTanksLevelsIni, lDescription);
//	}
	
	if(ioSystem.getRegister().isRegistered("sim.dynamic.timestep")) {
		mContinuousTimeStep = castHandleT<Float>(ioSystem.getRegister()["sim.dynamic.timestep"]);
	} else {
		mContinuousTimeStep = new Float(1e-4);
		Register::Description lDescription(
										   "Dynamic time step",
										   "Float",
										   mContinuousTimeStep->serialize(),
										   "Dynamic time step"
										   );
		ioSystem.getRegister().addEntry("sim.dynamic.timestep", mContinuousTimeStep, lDescription);
	}
	
	if(ioSystem.getRegister().isRegistered("sim.duration.time")) {
		mSimulationDuration = castHandleT<Float>(ioSystem.getRegister()["sim.duration.time"]);
	} else {
		mSimulationDuration = new Float(15);
		Register::Description lDescription(
										   "Simulation duration",
										   "Float",
										   mSimulationDuration->serialize(),
										   "Simulation duration"
										   );
		ioSystem.getRegister().addEntry("sim.duration.time", mSimulationDuration, lDescription);
	}
	
	if(ioSystem.getRegister().isRegistered("eval.penalty.samesource")) {
		mPenaltyFactor = castHandleT<Float>(ioSystem.getRegister()["eval.penalty.samesource"]);
	} else {
		mPenaltyFactor = new Float(0.75);
		Register::Description lDescription(
										   "Penalty factor applied to the fitness if one output is the same as the source of to the other output. 1.0 means no penalty",
										   "Float",
										   mPenaltyFactor->serialize(),
										   "Penalty factor"
										   );
		ioSystem.getRegister().addEntry("eval.penalty.samesource", mContinuousTimeStep, lDescription);
	}
}


/*!
 *  \brief Post-initialize the evaluation operator.
 *  \param ioSystem Evolutionary system.
 */
void DCDCBoostEvalOp::postInit(Beagle::System& ioSystem)
{
#ifdef USE_MPI
	Beagle::MPI::EvaluationOp::postInit(ioSystem);
#else
	Beagle::EvaluationOp::postInit(ioSystem);
#endif
	
	ioSystem.addComponent(new ParametersHolder);
	
//	if(*mTargetString == Beagle::String("")) {
//		SimulationCase lCase;
//		vector<double> lLimits(2);	lLimits[0] = 0.1; lLimits[1] = 0.5;
//		vector<double> lTimes(1,0);
//		lCase.createRandomCase(&ioSystem.getRandomizer(),NBOUTPUTS,lLimits,lTimes);
//		ostringstream lStream;
//		lCase.write(lStream);
//		mSimulationCases.push_back(lCase);
//		mTargetString = new String(lStream.str());
//	} 
	
	readSimulationCase(*mTargetString, mSimulationCases);
	
	if(mSimulationCases[0].getTime(0) != 0) {
		throw Beagle_RunTimeExceptionM("DCDCBoostEvalOp : Not applying control target at time 0");
	}
	
	if(mGenerationSteps->size() > 0) {
		if((*mGenerationSteps)[0] >= 0) {
			if((*mGenerationSteps)[0] != 0) {
				throw Beagle_RunTimeExceptionM("DCDCBoostEvalOp : Not applying control target at generation 0");
			}
		}
	}
}

#include "rosplan_interface_movebase/RPMoveBase.h"

/* The implementation of RPMoveBase.h */
namespace KCL_rosplan {

	/* constructor */
	RPMoveBase::RPMoveBase(ros::NodeHandle &nh, std::string &actionserver)
	 : message_store(nh), action_client(actionserver, true) {

		// costmap client
		clear_costmaps_client = nh.serviceClient<std_srvs::Empty>("/move_base/clear_costmaps");

		// create the action client
		ROS_INFO("KCL: (%s) waiting for action server to start on %s", params.name.c_str(), actionserver.c_str());
		// action_client.waitForServer();
	}

	/* action dispatch callback */
	bool RPMoveBase::concreteCallback(const rosplan_dispatch_msgs::ActionDispatch::ConstPtr& msg) {

		// get waypoint ID from action dispatch
		std::string wpID;
		bool found = false;
		for(size_t i=0; i<msg->parameters.size(); i++) {
			if(0==msg->parameters[i].key.compare("to")) {
				wpID = msg->parameters[i].value;
				found = true;
			}
		}
		if(!found) {
			ROS_INFO("KCL: (%s) aborting action dispatch; PDDL action missing required parameter ?to", params.name.c_str());
			return false;
		}
		
		// get pose from message store
		std::vector< boost::shared_ptr<geometry_msgs::PoseStamped> > results;
		if(message_store.queryNamed<geometry_msgs::PoseStamped>(wpID, results)) {
			if(results.size()<1) {
				ROS_INFO("KCL: (%s) aborting action dispatch; no matching wpID %s", params.name.c_str(), wpID.c_str());
				return false;
			}
			if(results.size()>1)
				ROS_INFO("KCL: (%s) multiple waypoints share the same wpID", params.name.c_str());

			// dispatch MoveBase action
			move_base_msgs::MoveBaseGoal goal;
			geometry_msgs::PoseStamped &pose = *results[0];
			goal.target_pose = pose;
			action_client.sendGoal(goal);

			bool finished_before_timeout = action_client.waitForResult();
			if (finished_before_timeout) {

				actionlib::SimpleClientGoalState state = action_client.getState();
				ROS_INFO("KCL: (%s) action finished: %s", params.name.c_str(), state.toString().c_str());

				if(state == actionlib::SimpleClientGoalState::SUCCEEDED) {

					// remove old robot_at
					rosplan_knowledge_msgs::KnowledgeUpdateService updatePredSrv;
					updatePredSrv.request.knowledge.knowledge_type = rosplan_knowledge_msgs::KnowledgeItem::FACT;
					updatePredSrv.request.update_type = rosplan_knowledge_msgs::KnowledgeUpdateService::Request::REMOVE_KNOWLEDGE;
					updatePredSrv.request.knowledge.attribute_name = "robot_at";
					diagnostic_msgs::KeyValue pair;
					pair.key = "v";
					pair.value = "kenny";
					updatePredSrv.request.knowledge.values.push_back(pair);
					update_knowledge_client.call(updatePredSrv);

					// predicate robot_at
					updatePredSrv.request.update_type = rosplan_knowledge_msgs::KnowledgeUpdateService::Request::ADD_KNOWLEDGE;
					updatePredSrv.request.knowledge.attribute_name = "robot_at";
					diagnostic_msgs::KeyValue pairWP;
					pairWP.key = "wp";
					pairWP.value = wpID;
					updatePredSrv.request.knowledge.values.push_back(pairWP);
					update_knowledge_client.call(updatePredSrv);

					ros::Rate big_rate(0.5);
					big_rate.sleep();

					// publish feedback (achieved)
					return true;

				} else {

					// clear costmaps
					std_srvs::Empty emptySrv;
					clear_costmaps_client.call(emptySrv);

					// publish feedback (failed)
					return false;
				}
			} else {
				// timed out (failed)
				action_client.cancelAllGoals();
				ROS_INFO("KCL: (%s) action timed out", params.name.c_str());
				return false;
			}
		} else {
			// no KMS connection (failed)
			ROS_INFO("KCL: (%s) aborting action dispatch; query to sceneDB failed", params.name.c_str());
			return false;
		}
	}
} // close namespace

	/*-------------*/
	/* Main method */
	/*-------------*/

	int main(int argc, char **argv) {

		ros::init(argc, argv, "rosplan_interface_movebase");
		ros::NodeHandle nh("~");

		std::string actionserver;
		nh.param("action_server", actionserver, std::string("/move_base"));

		// create PDDL action subscriber
		KCL_rosplan::RPMoveBase rpmb(nh, actionserver);

		// listen for action dispatch
		ros::Subscriber ds = nh.subscribe("/kcl_rosplan/action_dispatch", 1000, &KCL_rosplan::RPActionInterface::dispatchCallback, dynamic_cast<KCL_rosplan::RPActionInterface*>(&rpmb));
		rpmb.runActionInterface();

		return 0;
	}

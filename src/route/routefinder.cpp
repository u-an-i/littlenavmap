/*****************************************************************************
* Copyright 2015-2016 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "routefinder.h"
#include "routenetworkradio.h"
#include "geo/calculations.h"

const float COST_FACTOR_UNREACHABLE_RADIONAV = 2.f;
const float COST_FACTOR_NDB = 2.f;
const float COST_FACTOR_VOR = 2.f;
const float COST_FACTOR_DME = 4.f;

using nw::Node;
using atools::geo::Pos;

RouteFinder::RouteFinder(RouteNetworkRadio *routeNetwork)
  : network(routeNetwork)
{

}

RouteFinder::~RouteFinder()
{

}

void RouteFinder::calculateRoute(const atools::geo::Pos& from, const atools::geo::Pos& to,
                                 QVector<maptypes::MapObjectRef>& route)
{
  network->addStartAndDestinationNodes(from, to);
  Node startNode = network->getStartNode();
  Node destNode = network->getDestinationNode();

  int numNodesTotal = network->getNumberOfNodesDatabase();

  if(startNode.edges.isEmpty())
    return;

  openNodesHeap.push(startNode, 0.f);
  nodeCosts[startNode.id] = 0.f;

  Node currentNode;
  bool found = false;
  while(!openNodesHeap.isEmpty())
  {
    // Contains known nodes
    openNodesHeap.pop(currentNode);

    if(currentNode.id == destNode.id)
    {
      found = true;
      break;
    }

    // Contains nodes with known shortest path
    closedNodes.insert(currentNode.id);

    if(closedNodes.size() > numNodesTotal / 2)
      // If we read too much nodes routing will fail
      break;

    // Work on successors
    expandNode(currentNode, destNode);
  }

  qDebug() << "heap size" << openNodesHeap.size();
  qDebug() << "close nodes size" << closedNodes.size();

  if(found)
  {
    // Build route
    int predId = currentNode.id;
    while(predId != -1)
    {
      int navId;
      nw::NodeType type;
      network->getNavIdAndTypeForNode(predId, navId, type);

      if(type != nw::START && type != nw::DESTINATION)
        route.append({navId, toMapObjectType(type)});

      predId = nodePredecessor.value(predId, -1);
    }
  }
  else
    qDebug() << "No route found";

  qDebug() << "num nodes database" << network->getNumberOfNodesDatabase()
           << "num nodes cache" << network->getNumberOfNodesCache();
}

void RouteFinder::expandNode(const nw::Node& currentNode, const nw::Node& destNode)
{
  QVector<Node> successors;
  QVector<int> distancesMeter;

  network->getNeighbours(currentNode, successors, distancesMeter);

  for(int i = 0; i < successors.size(); i++)
  {
    const Node& successor = successors.at(i);

    if(closedNodes.contains(successor.id))
      continue;

    int distanceMeter = distancesMeter.at(i);

    float successorEdgeCosts = cost(currentNode, successor, distanceMeter);
    float successorNodeCosts = nodeCosts.value(currentNode.id) + successorEdgeCosts;

    if(successorNodeCosts >= nodeCosts.value(successor.id) && openNodesHeap.contains(successor))
      // New path is not cheaper
      continue;

    nodePredecessor[successor.id] = currentNode.id;
    nodeCosts[successor.id] = successorNodeCosts;

    // Costs from start to successor + estimate to destination = sort order in heap
    float f = successorNodeCosts + costEstimate(successor, destNode);

    if(openNodesHeap.contains(successor))
      openNodesHeap.change(successor, f);
    else
      openNodesHeap.push(successor, f);
  }
}

float RouteFinder::cost(const nw::Node& currentNode, const nw::Node& successor, int distanceMeter)
{
  float costs = distanceMeter;

  if(currentNode.range + successor.range < distanceMeter)
    costs *= COST_FACTOR_UNREACHABLE_RADIONAV;

  if(successor.type == nw::DME)
    costs *= COST_FACTOR_DME;
  else if(successor.type == nw::VOR)
    costs *= COST_FACTOR_VOR;
  else if(successor.type == nw::NDB)
    costs *= COST_FACTOR_NDB;

  return costs;
}

float RouteFinder::costEstimate(const nw::Node& currentNode, const nw::Node& successor)
{
  // return 0.f;
  // Use largest factor to allow underestimate
  return currentNode.pos.distanceMeterTo(successor.pos);
}

maptypes::MapObjectTypes RouteFinder::toMapObjectType(nw::NodeType type)
{
  switch(type)
  {
    case nw::WAYPOINT_JET:
    case nw::WAYPOINT_VICTOR:
    case nw::WAYPOINT_BOTH:
      return maptypes::WAYPOINT;

    case nw::VOR:
    case nw::VORDME:
    case nw::DME:
      return maptypes::VOR;

    case nw::NDB:
      return maptypes::NDB;

    case nw::START:
    case nw::DESTINATION:
      return maptypes::USER;

    case nw::NONE:
      break;
  }
  return maptypes::NONE;
}

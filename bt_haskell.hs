import qualified Data.List
data (Ord a, Eq a) => Tree a = Nil | Node (Tree a) a (Tree a) 
	deriving Show
empty :: (Ord a) => Tree a -> Bool
empty Nil = True
empty  _  = False
contains :: (Ord a) => (Tree a) -> a -> Bool
contains Nil _ = False
contains (Node t1 v t2) x 
	| x == v = True
	| x  < v = contains t1 x 
	| x  > v = contains t2 x
insert :: (Ord a) => Tree a -> a -> Tree a
insert Nil x = Node Nil x Nil
insert (Node t1 v t2) x 
	| v == x = Node t1 v t2
	| v  < x = Node t1 v (insert t2 x)
	| v  > x = Node (insert t1 x) v t2
delete :: (Ord a) => Tree a -> a -> Tree a
delete Nil _ = Nil
delete (Node t1 v t2) x  
	| x == v = deleteX (Node t1 v t2)
	| x  < v = Node (delete t1 x) v t2
	| x  > v = Node t1 v (delete t2 x)
deleteX :: (Ord a) => Tree a -> Tree a 
deleteX (Node Nil v t2) = t2
deleteX (Node t1 v Nil) = t1
deleteX (Node t1 v t2) = (Node t1 v2 t2) --(delete t2 v2))
	where 
		v2 = leftistElement t2
leftistElement :: (Ord a) => Tree a -> a
leftistElement (Node Nil v _) = v
leftistElement (Node t1 _ _) = leftistElement t1
ctree :: (Ord a) => [a] -> Tree a
ctree [] = Nil
ctree (h:t) = ctree2 (Node Nil h Nil) t
	where
		ctree2 tr [] = tr
		ctree2 tr (h:t) = ctree2 (insert tr h) t
ctreePB :: (Ord a) => [a] -> Tree a
ctreePB [] = Nil
ctreePB s = cpb Nil (qsort s) 
cpb :: (Ord a) => Tree a -> [a] -> Tree a
cpb tr [] = tr
cpb tr t = cpb (insert tr e) t2
	where	
		e = middleEl t
		t2 = Data.List.delete e t
middleEl :: (Ord a) => [a] -> a
middleEl s = mEl s s 
mEl :: (Ord a) => [a] ->  [a] -> a
mEl    []    (h:s2) = h
mEl (_:[])   (h:s2) = h
mEl (_:_:s1) (_:s2) = mEl s1 s2
inorder :: (Ord a) => Tree a -> [a]
inorder Nil = []
inorder (Node t1 v t2) = inorder t1 ++ [v] ++ inorder t2
preorder :: (Ord a) => Tree a -> [a]
preorder Nil = []
preorder (Node t1 v t2) = [v] ++ preorder t1 ++ preorder t2
postorder :: (Ord a) => Tree a -> [a]
postorder Nil = []
postorder (Node t1 v t2) = postorder t1 ++ postorder t2 ++ [v]
levelorder :: (Ord a) => Tree a -> [a]
levelorder t = step [t]
	where
		step [] = []
		step ts = concatMap elements ts ++ step (concatMap subtrees ts)
		elements Nil = []
		elements (Node left x right) = [x]
		subtrees Nil = []
		subtrees (Node left x right) = [left,right]
qsort :: (Ord a) => [a] -> [a] 
qsort [] = []
qsort (h:t) = (qsort [x| x<-t, x < h]) ++ [h] ++ (qsort [x| x<-t, x>=h ])
